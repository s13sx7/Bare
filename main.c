#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#define BIT(X) (1UL << (X))
#define PIN(bank, num) ((((bank)-'A') << 8) | (num))
#define PINNO(pin) (pin & 255)
#define PINBANK(pin) (pin >> 8)


///for mode 00 - input
#define GPIO_MODE_INPUT_ANALOG 0x00 // CNF- 00
#define GPIO_MODE_INPUT_FLOATING 0x04 // CNF - 01
#define GPIO_MODE_INPUT_PULL 0x08 // CNF - 10

//for mode 01 - output 10MHz
#define GPIO_MODE_OUTPUT_10M_GPPUSH 0x01 //CNF - 00
#define GPIO_MODE_OUTPUT_10M_GPOPEND 0x05 //CNF - 01
#define GPIO_MODE_OUTPUT_10M_AFPUSH 0x09 //CNF - 10
#define GPIO_MODE_OUTPUT_10M_AFOPEND 0x0D //CNF - 11

//for mode 10 - output 2MHz
#define GPIO_MODE_OUTPUT_2M_GPPUSH 0x02 //CNF -00
#define GPIO_MODE_OUTPUT_2M_GPOPEND 0x06 //CNF -01
#define GPIO_MODE_OUTPUT_2M_AFPUSH 0x0A //CNF -10
#define GPIO_MODE_OUTPUT_2M_AFOPEND 0x0E //CNF - 11
//for mode 11 - output 50Mhz
#define GPIO_MODE_OUTPUT_50M_GPPUSH   0x03  // CNF=00
#define GPIO_MODE_OUTPUT_50M_GPOPEND  0x07  // CNF=01
#define GPIO_MODE_OUTPUT_50M_AFPUSH   0x0B  // CNF=10
#define GPIO_MODE_OUTPUT_50M_AFOPEND  0x0F  // CNF=11

#define CORE_FREQ 72000000 // CORE FREQUENCY

struct gpio {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, LCKR;
    volatile uint16_t BRR;
};
#define GPIO(bank) ((struct gpio *) (0x40010800 + 0x400*(bank)))

static inline void gpio_set_mode(uint16_t pin, uint8_t mode) {
  struct gpio *gpio = GPIO(PINBANK(pin));  // GPIO bank
  int n = PINNO(pin);                      // Pin number
  if (n < 8){
    gpio->CRL &= ~(15U << (n * 4));         // Clear existing setting
    gpio->CRL |= (mode & 15U) << (n * 4);    // Set new mode
  }else{
    n = n-8;
    gpio->CRH &= ~(15U << (n * 4));         // Clear existing setting
    gpio->CRH |= (mode & 15U) << (n * 4);  
  }
};

struct rcc{
    volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR, 
        APB1ENR, BDCR, CSR;
};
#define RCC ((struct rcc *) 0x40021000)

struct pwr{
    volatile uint32_t CR, CSR;
};
#define PWR ((struct pwr *) 0x40007000)

struct systick{
    volatile uint32_t CSR, RVR, CVR, CALIB;
};
#define SYSTICK ((struct systick *) 0xE000E010)

struct usart{
  volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;  
};
#define USART1 ((struct usart*) 0x40013800) // rcc reg: APB2ENR(14) PA9(TX) PA10(RX)
#define USART2 ((struct usart*) 0x40004400)// rcc reg: APB1ENR(16)
#define USART3 ((struct usart*) 0x40004800)// rcc reg: APB1ENR(17)

static inline void spin(volatile uint32_t count) {
  while (count--);
}

static inline void usart_init(struct usart *usart, unsigned long baud) {
    uint16_t rx, tx;

    if (usart == USART1) RCC->APB2ENR |= BIT(14);
    if (usart == USART2) RCC->APB1ENR |= BIT(16);
    if (usart == USART3) RCC->APB1ENR |= BIT(17);

    if (usart == USART1) rx = PIN('A', 10), tx = PIN('A', 9);
    if (usart == USART2) (void) 0; 
    if (usart == USART3) (void) 0;
    RCC->APB2ENR |= BIT(2); // GPIOAEN
    gpio_set_mode(rx, GPIO_MODE_INPUT_PULL);
    gpio_set_mode(tx, GPIO_MODE_OUTPUT_50M_AFPUSH);
    
    usart->CR1 = 0;
    usart->CR1 |= BIT(13) | BIT(3) | BIT(2);
    usart->BRR = CORE_FREQ / baud;
}

static inline int usart_read_ready(struct usart *usart) {
  return usart->SR & BIT(5);  // If RXNE bit is set, data is ready
}

static inline uint8_t usart_read_byte(struct usart *usart){
    return (uint8_t) (usart->DR & 255);
}

static inline void usart_write_byte(struct usart *usart, uint8_t byte) {
    usart->DR = byte;
    while ((usart->SR & BIT(7)) == 0) spin(1);
}

static inline void usart_write_buf(struct usart *usart, char *buf, size_t len) {
  while (len-- > 0) usart_write_byte(usart, *(uint8_t *) buf++);
}

static inline void systick_init(uint32_t ticks) {
    if ((ticks - 1) > 0xffffff) return;
    SYSTICK->RVR = ticks - 1;
    SYSTICK->CVR = 0;
    SYSTICK->CSR |= BIT(0) | BIT(1) | BIT(2);
}

static inline void gpio_write(uint16_t pin, bool val){
    struct gpio *gpio = GPIO(PINBANK(pin));
    gpio->BSRR = (1U << PINNO(pin)) << (val ? 0 : 16);
}

static volatile uint32_t s_ticks; // volatile is important!!
void SysTick_Handler(void) { // This is interupt handler for SysTick
  s_ticks++;
}
#if 1

// t: expiration time, prd: period, now: current time. Return true if expired
bool timer_expired(uint32_t *t, uint32_t prd, uint32_t now) {
  if (now + prd < *t) *t = 0;                    // Time wrapped? Reset timer
  if (*t == 0) *t = now + prd;                   // First poll? Set expiration
  if (*t > now) return false;                    // Not expired yet, return
  *t = (now - *t) > prd ? now + prd : *t + prd;  // Next expiration time
  return true;                                   // Expired, return true
}

#endif
int main(void){
    RCC->APB1ENR |= BIT(28);   // PWREN – включаем тактирование PWR
    PWR->CR |= BIT(8); // ЭТО ВСЁ ПОТОМУ ЧТО PC13 ПИН НЕ ТАКОЙ ПРОСТОЙ, ПОЧИТАЙ
    uint16_t led = PIN('C',13);                
    RCC->APB2ENR |= BIT(4);
    gpio_set_mode(led, GPIO_MODE_OUTPUT_2M_GPPUSH);
    systick_init(16000000/ 1000);
    uint32_t timer, period = 500;
    //usart_init(USART1, 115200);
    for(;;){
        if (timer_expired(&timer, period, s_ticks)){
            static bool on;
            gpio_write(led, on);
            on = !on;
            //usart_write_buf(USART1, "hi\r\n", 4); 
        }
    }
    return 0;
}

// Startup code
__attribute__((naked, noreturn)) void _reset(void) {
  // memset .bss to zero, and copy .data section to RAM region
  extern long _sbss, _ebss, _sdata, _edata, _sidata;
  for (long *dst = &_sbss; dst < &_ebss; dst++) *dst = 0;
  for (long *dst = &_sdata, *src = &_sidata; dst < &_edata;) *dst++ = *src++;
}

extern void _estack(void);  // Defined in xx.ld

// 16 standard and 68 STM32-specific interupt handlers
__attribute__((section(".vectors"))) void (*const tab[16 + 68])(void) = {
  _estack, _reset, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, SysTick_Handler};
