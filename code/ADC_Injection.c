#include "ADC_Injection.h"

uint32_t TIM2_interrupts = 0;

void ADC_Init_Injection () {
		RCC->APB2ENR |= RCC_APB2ENR_IOPAEN ;							// Port A clock enable
		GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0); 	// PA0 - analog input, ADC channel 0
		GPIOA->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);  // PA1 - analog input, ADC channel 1
		RCC->APB2ENR |= RCC_APB2ENR_ADC1EN; 							// ADC clock enable
		
		RCC->CFGR &= ~RCC_CFGR_ADCPRE;										// clear ADCPRE bits
		RCC->CFGR |=  RCC_CFGR_ADCPRE_1;									// ADC prescaler = 6 (10b in ADCPRE field of CFGR) for 72/6 = 12 MHz ADC
	
		ADC1->CR1 = 0x0;																	// Clear CR1
		ADC1->CR2 = 0x0;  																// Clear CR2
			
		ADC1->SMPR2 |= ADC_SMPR2_SMP0_0 | ADC_SMPR2_SMP0_1 ; // Sample time is 28,5 cycles (~2.3 mcs with 12 MHz)  for channel 0
		ADC1->SMPR2 |= ADC_SMPR2_SMP1_0 | ADC_SMPR2_SMP1_1 ; // Sample time is 28,5 cycles (~2.3 mcs with 12 MHz)  for channel 1
	
		ADC1->SQR1 = 0; 																	// One regular channel
		ADC1->SQR3 = 0; 																	// First channel is regular channel 0
		ADC1->JSQR = 0; 																	// One injection channel
		ADC1->JSQR |= ADC_JSQR_JSQ4_0; 										// Second channel is injection channel 1
	
		ADC1->CR2 |= ADC_CR2_EXTSEL ; 										// Software start is source for regular channels
		ADC1->CR2 |= ADC_CR2_EXTTRIG;											// External start enable
		
		ADC1->CR2 |= ADC_CR2_JEXTSEL ; 										// Software start is source for injection channels
		ADC1->CR2 |= ADC_CR2_JEXTTRIG;										// External start enable
		
		ADC1->CR2 &= ~ADC_CR2_CONT;												// One conversion, disable sequence conversion 
		ADC1->CR1 |= ADC_CR1_SCAN;												// Enable channels scan
	
		ADC1->CR2 |= ADC_CR2_ADON; 												// ADC ON
	
		ADC1->CR2 |= ADC_CR2_CAL; 												// Start calibration 
		while ((ADC1->CR2 & ADC_CR2_CAL) != 0) ;					// Check if calibration is finished
}


void TIM2_Init () {																 		// TIM2 using for start conversion for injection channels
	  RCC -> APB1ENR |= RCC_APB1ENR_TIM2EN; 				 		// TIM2 clock enable
		TIM2 -> CR1 = TIM_CR1_CEN;										 		// TIM2 enable
				
		TIM2->DIER |= TIM_DIER_UIE; 									 		// Enable TIM2 interrupt
		NVIC_EnableIRQ (TIM2_IRQn);										 		// Enable TIM2 interrupt	in interrupt controller
}

void ADC_Convert_JIT () {
		NVIC_EnableIRQ (ADC1_2_IRQn);									 		// Enable ADC interrupt in interrupt controller
		ADC1->CR1 |= ADC_CR1_EOCIE;										 		// Enable interrupt when conversion is finished
}

void TIM2_IRQHandler () { 												 		// TIM2 interrupr handler
		TIM2_interrupts++; 														 		// TIM2 interrupts counter
		if ( (TIM2_interrupts % 1000000) == 0) {
				ADC1->CR2 |= ADC_CR2_SWSTART;
				ADC1->CR2 |= ADC_CR2_JSWSTART; 	
		}
}