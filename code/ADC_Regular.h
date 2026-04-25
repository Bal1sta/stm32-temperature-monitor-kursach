#include "stm32f10x.h"

#define VREF 3.3

void ADC_Init () ;
void TIM3_Init ();
void ADC_TIM3_Init ();
void ADC_Convert ();
void ADC_Convert_SQ ();
void ADC_Convert_IT ();
