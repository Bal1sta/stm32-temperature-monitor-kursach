#include "stm32f10x.h"
#include <stdio.h>

/* ---------------- CONFIG ---------------- */

#define SYSCLK 72000000UL

#define TC77_CS 4

#define BTN_START_PIN 0
#define BTN_STOP_PIN  1
#define BTN_RESET_PIN 11

#define DIGIT1_PIN 0
#define DIGIT2_PIN 1
#define DIGIT3_PIN 2
#define DIGIT4_PIN 3

#define SEG_A_PIN 4
#define SEG_B_PIN 5
#define SEG_C_PIN 6
#define SEG_D_PIN 7
#define SEG_E_PIN 8
#define SEG_F_PIN 9
#define SEG_G_PIN 10
#define SEG_DP_PIN 11

#define LED_PIN 8

#define TC77_RESOLUTION 0.0625f
#define TEMP_THRESHOLD 25.0f

volatile uint32_t msTicks = 0;

volatile uint8_t displayDigits[4] = {0, 0, 0, 0};
volatile uint8_t displayDot[4] = {0, 0, 0, 0};
volatile uint8_t currentDigit = 0;

volatile uint8_t programRunning = 0;
volatile uint8_t resetRequest = 0;

/* ---------------- PROTOTYPES ---------------- */

float TC77_ReadTemperature(void);
void Display_PrepareFloat(float value);
void PrintText(const char *text);

/* ---------------- SYSTICK ---------------- */

void SysTick_Handler(void)
{
    msTicks++;
}

void DelayMs(uint32_t ms)
{
    uint32_t start = msTicks;
    while ((msTicks - start) < ms) {
        __NOP();
    }
}

/* ---------------- CLOCK ---------------- */

void SystemCoreClockConfigure(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    FLASH->ACR |= FLASH_ACR_PRFTBE;
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_2;

    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

/* ---------------- USART2 57600 ---------------- */

void USART_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    GPIOA->CRL &= ~(0xF << 8);
    GPIOA->CRL |=  (0xB << 8);   /* PA2 TX */

    GPIOA->CRL &= ~(0xF << 12);
    GPIOA->CRL |=  (0x4 << 12);  /* PA3 RX */

    USART2->BRR = 0x0271;        /* 57600, APB1 = 36 MHz */

    USART2->CR1 = USART_CR1_TE |
                  USART_CR1_RE |
                  USART_CR1_UE |
                  USART_CR1_RXNEIE;

    NVIC_EnableIRQ(USART2_IRQn);
}

void PrintText(const char *text)
{
    while (*text) {
        while (!(USART2->SR & USART_SR_TXE));
        USART2->DR = *text++;
    }
}

void PrintFloat(float value)
{
    char buffer[32];
    int intPart = (int)value;
    int fracPart = (int)((value - intPart) * 10);

    if (fracPart < 0) fracPart = -fracPart;

    sprintf(buffer, "TEMP = %d.%d C\r\n", intPart, fracPart);
    PrintText(buffer);
}

/* ---------------- GPIO ---------------- */

void GPIO_Init_All(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_AFIOEN;

    /* LED PA8 */
    GPIOA->CRH &= ~(0xF << 0);
    GPIOA->CRH |=  (0x3 << 0);

    /* Buttons PA0, PA1 input pull-up */
    GPIOA->CRL &= ~((0xF << 0) | (0xF << 4));
    GPIOA->CRL |=  ((0x8 << 0) | (0x8 << 4));
    GPIOA->ODR |= (1 << BTN_START_PIN) | (1 << BTN_STOP_PIN);

    /* Button PA11 input pull-up */
    GPIOA->CRH &= ~(0xF << 12);
    GPIOA->CRH |=  (0x8 << 12);
    GPIOA->ODR |= (1 << BTN_RESET_PIN);

    /* TC77 CS PA4 */
    GPIOA->CRL &= ~(0xF << (TC77_CS * 4));
    GPIOA->CRL |=  (0x3 << (TC77_CS * 4));
    GPIOA->BSRR = (1 << TC77_CS);

    /* Display PB0-PB7 */
    GPIOB->CRL = 0x33333333;

    /* Display PB8-PB11 */
    GPIOB->CRH &= ~(0xFFFF << 0);
    GPIOB->CRH |=  (0x3333 << 0);

    GPIOB->BSRR = (1 << DIGIT1_PIN) |
                  (1 << DIGIT2_PIN) |
                  (1 << DIGIT3_PIN) |
                  (1 << DIGIT4_PIN);

    /* SPI1 PA5 SCK */
    GPIOA->CRL &= ~(0xF << 20);
    GPIOA->CRL |=  (0xB << 20);

    /* SPI1 PA6 MISO */
    GPIOA->CRL &= ~(0xF << 24);
    GPIOA->CRL |=  (0x4 << 24);

    /* SPI1 PA7 MOSI */
    GPIOA->CRL &= ~(0xF << 28);
    GPIOA->CRL |=  (0xB << 28);
}

/* ---------------- PROGRAM CONTROL ---------------- */

uint8_t ButtonPressed(uint8_t pin)
{
    return ((GPIOA->IDR & (1 << pin)) == 0);
}

void Program_Start(void)
{
    programRunning = 1;
    PrintText("START\r\n");
}

void Program_Stop(void)
{
    programRunning = 0;
    GPIOA->BRR = (1 << LED_PIN);
    PrintText("STOP\r\n");
}

void Program_Reset(void)
{
    PrintText("RESET\r\n");
    resetRequest = 1;
}

void Buttons_Process(void)
{
    static uint8_t startPrev = 0;
    static uint8_t stopPrev = 0;
    static uint8_t resetPrev = 0;
    static uint32_t lastCheck = 0;

    uint8_t startNow;
    uint8_t stopNow;
    uint8_t resetNow;

    if ((msTicks - lastCheck) < 30) return;
    lastCheck = msTicks;

    startNow = ButtonPressed(BTN_START_PIN);
    stopNow = ButtonPressed(BTN_STOP_PIN);
    resetNow = ButtonPressed(BTN_RESET_PIN);

    if (startPrev && !startNow) Program_Start();
    if (stopPrev && !stopNow) Program_Stop();
    if (resetPrev && !resetNow) Program_Reset();

    startPrev = startNow;
    stopPrev = stopNow;
    resetPrev = resetNow;
}

/* ---------------- USART IRQ ---------------- */

void USART2_IRQHandler(void)
{
    if (USART2->SR & USART_SR_RXNE) {
        char symbol = (char)USART2->DR;

        if (symbol == '\r' || symbol == '\n' || symbol == ' ') {
            return;
        }

        if (symbol == 's' || symbol == 'S') {
            Program_Start();
        }
        else if (symbol == 'p' || symbol == 'P') {
            Program_Stop();
        }
        else if (symbol == 'r' || symbol == 'R') {
            Program_Reset();
        }
        else if (symbol == 't' || symbol == 'T') {
            if (programRunning) {
                float temp = TC77_ReadTemperature();
                PrintFloat(temp);
            } else {
                PrintText("PROGRAM STOPPED\r\n");
            }
        }
        else {
            PrintText("UNKNOWN COMMAND\r\n");
        }
    }
}

/* ---------------- SPI ---------------- */

void SPI1_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    SPI1->CR1 = SPI_CR1_MSTR |
                SPI_CR1_BR_1 |
                SPI_CR1_BR_2 |
                SPI_CR1_SSM |
                SPI_CR1_SSI |
                SPI_CR1_SPE;
}

void SPI_Set16Bit(void)
{
    SPI1->CR1 |= SPI_CR1_DFF;
}

uint16_t SPI_Send16(uint16_t data)
{
    while (!(SPI1->SR & SPI_SR_TXE));
    SPI1->DR = data;

    while (!(SPI1->SR & SPI_SR_RXNE));
    while (SPI1->SR & SPI_SR_BSY);

    return SPI1->DR;
}

/* ---------------- TC77 ---------------- */

float TC77_ReadTemperature(void)
{
    uint16_t raw;
    int16_t t;

    SPI_Set16Bit();

    GPIOA->BRR = (1 << TC77_CS);
    raw = SPI_Send16(0x0000);
    GPIOA->BSRR = (1 << TC77_CS);

    t = raw >> 3;

    if (t & 0x1000) {
        t |= 0xE000;
    }

    return t * TC77_RESOLUTION;
}

/* ---------------- DISPLAY ---------------- */

const uint8_t segmentTable[10] = {
    ~0x3F,
    ~0x06,
    ~0x5B,
    ~0x4F,
    ~0x66,
    ~0x6D,
    ~0x7D,
    ~0x07,
    ~0x7F,
    ~0x6F
};

const uint8_t minusPattern = ~0x40;

void Display_SetSegments(uint8_t segments)
{
    GPIOB->BRR = (1 << SEG_A_PIN) |
                 (1 << SEG_B_PIN) |
                 (1 << SEG_C_PIN) |
                 (1 << SEG_D_PIN) |
                 (1 << SEG_E_PIN) |
                 (1 << SEG_F_PIN) |
                 (1 << SEG_G_PIN) |
                 (1 << SEG_DP_PIN);

    if (!(segments & 0x01)) GPIOB->BSRR = (1 << SEG_A_PIN);
    if (!(segments & 0x02)) GPIOB->BSRR = (1 << SEG_B_PIN);
    if (!(segments & 0x04)) GPIOB->BSRR = (1 << SEG_C_PIN);
    if (!(segments & 0x08)) GPIOB->BSRR = (1 << SEG_D_PIN);
    if (!(segments & 0x10)) GPIOB->BSRR = (1 << SEG_E_PIN);
    if (!(segments & 0x20)) GPIOB->BSRR = (1 << SEG_F_PIN);
    if (!(segments & 0x40)) GPIOB->BSRR = (1 << SEG_G_PIN);
    if (!(segments & 0x80)) GPIOB->BSRR = (1 << SEG_DP_PIN);
}

void Display_EnableDigit(uint8_t digit)
{
    GPIOB->BSRR = (1 << DIGIT1_PIN) |
                  (1 << DIGIT2_PIN) |
                  (1 << DIGIT3_PIN) |
                  (1 << DIGIT4_PIN);

    switch (digit) {
        case 0: GPIOB->BRR = (1 << DIGIT1_PIN); break;
        case 1: GPIOB->BRR = (1 << DIGIT2_PIN); break;
        case 2: GPIOB->BRR = (1 << DIGIT3_PIN); break;
        case 3: GPIOB->BRR = (1 << DIGIT4_PIN); break;
    }
}

void Display_UpdateDigit(void)
{
    uint8_t segments;

    if (displayDigits[currentDigit] == 10) {
        segments = minusPattern;
    } else {
        segments = segmentTable[displayDigits[currentDigit]];
    }

    if (displayDot[currentDigit]) {
        segments &= ~0x80;
    } else {
        segments |= 0x80;
    }

    Display_SetSegments(segments);
    Display_EnableDigit(currentDigit);

    currentDigit++;
    if (currentDigit >= 4) currentDigit = 0;
}

void Display_PrepareFloat(float value)
{
    int intPart;
    int fracPart;
    uint8_t digits[4] = {0, 0, 0, 0};
    uint8_t dotPos = 2;
    uint8_t isNegative = 0;
    uint8_t i;

    if (value < 0) {
        isNegative = 1;
        value = -value;
    }

    if (value > 999.9f) value = 999.9f;

    intPart = (int)value;
    fracPart = (int)((value - intPart) * 10);

    if (intPart >= 100) {
        digits[0] = intPart / 100;
        digits[1] = (intPart % 100) / 10;
        digits[2] = intPart % 10;
        digits[3] = fracPart;
        dotPos = 2;
    }
    else if (intPart >= 10) {
        digits[0] = intPart / 10;
        digits[1] = intPart % 10;
        digits[2] = fracPart;
        digits[3] = 0;
        dotPos = 1;
    }
    else {
        digits[0] = intPart;
        digits[1] = fracPart;
        digits[2] = 0;
        digits[3] = 0;
        dotPos = 0;
    }

    if (isNegative) {
        for (i = 3; i > 0; i--) {
            digits[i] = digits[i - 1];
        }

        digits[0] = 10;
        dotPos++;
        if (dotPos > 3) dotPos = 3;
    }

    for (i = 0; i < 4; i++) {
        displayDigits[i] = digits[i];
        displayDot[i] = (i == dotPos) ? 1 : 0;
    }
}

/* ---------------- MAIN ---------------- */

int main(void)
{
    float temp = 0.0f;
    uint32_t lastUpdate = 0;
    uint32_t lastDisplayUpdate = 0;

    SystemCoreClockConfigure();
    SysTick_Config(SYSCLK / 1000);

    GPIO_Init_All();
    SPI1_Init();
    USART_Init();

    Display_PrepareFloat(0.0f);

    PrintText("READY\r\n");
    PrintText("Commands: s=start, p=stop, r=reset, t=temp\r\n");

    while (1) {
        Buttons_Process();

        if (resetRequest) {
            DelayMs(100);
            NVIC_SystemReset();
        }

        if (programRunning) {
            if ((msTicks - lastUpdate) >= 500) {
                temp = TC77_ReadTemperature();
                lastUpdate = msTicks;

                Display_PrepareFloat(temp);

                if (temp > TEMP_THRESHOLD) {
                    GPIOA->ODR ^= (1 << LED_PIN);
                } else {
                    GPIOA->BRR = (1 << LED_PIN);
                }
            }
        }

        if ((msTicks - lastDisplayUpdate) >= 5) {
            Display_UpdateDigit();
            lastDisplayUpdate = msTicks;
        }
    }
}