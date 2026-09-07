################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (10.3-2021.10)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/accordion_tables.c \
../Core/Src/main.c \
../Core/Src/mcp23017.c \
../Core/Src/note.c \
../Core/Src/stm32f4xx_hal_msp.c \
../Core/Src/stm32f4xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f4xx.c \
../Core/Src/usbd_conf.c \
../Core/Src/usbd_core.c \
../Core/Src/usbd_ctlreq.c \
../Core/Src/usbd_desc.c \
../Core/Src/usbd_ioreq.c \
../Core/Src/usbd_midi.c \
../Core/Src/wavetable.c 

OBJS += \
./Core/Src/accordion_tables.o \
./Core/Src/main.o \
./Core/Src/mcp23017.o \
./Core/Src/note.o \
./Core/Src/stm32f4xx_hal_msp.o \
./Core/Src/stm32f4xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f4xx.o \
./Core/Src/usbd_conf.o \
./Core/Src/usbd_core.o \
./Core/Src/usbd_ctlreq.o \
./Core/Src/usbd_desc.o \
./Core/Src/usbd_ioreq.o \
./Core/Src/usbd_midi.o \
./Core/Src/wavetable.o 

C_DEPS += \
./Core/Src/accordion_tables.d \
./Core/Src/main.d \
./Core/Src/mcp23017.d \
./Core/Src/note.d \
./Core/Src/stm32f4xx_hal_msp.d \
./Core/Src/stm32f4xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f4xx.d \
./Core/Src/usbd_conf.d \
./Core/Src/usbd_core.d \
./Core/Src/usbd_ctlreq.d \
./Core/Src/usbd_desc.d \
./Core/Src/usbd_ioreq.d \
./Core/Src/usbd_midi.d \
./Core/Src/wavetable.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/accordion_tables.d ./Core/Src/accordion_tables.o ./Core/Src/accordion_tables.su ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/mcp23017.d ./Core/Src/mcp23017.o ./Core/Src/mcp23017.su ./Core/Src/note.d ./Core/Src/note.o ./Core/Src/note.su ./Core/Src/stm32f4xx_hal_msp.d ./Core/Src/stm32f4xx_hal_msp.o ./Core/Src/stm32f4xx_hal_msp.su ./Core/Src/stm32f4xx_it.d ./Core/Src/stm32f4xx_it.o ./Core/Src/stm32f4xx_it.su ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f4xx.d ./Core/Src/system_stm32f4xx.o ./Core/Src/system_stm32f4xx.su ./Core/Src/usbd_conf.d ./Core/Src/usbd_conf.o ./Core/Src/usbd_conf.su ./Core/Src/usbd_core.d ./Core/Src/usbd_core.o ./Core/Src/usbd_core.su ./Core/Src/usbd_ctlreq.d ./Core/Src/usbd_ctlreq.o ./Core/Src/usbd_ctlreq.su ./Core/Src/usbd_desc.d ./Core/Src/usbd_desc.o ./Core/Src/usbd_desc.su ./Core/Src/usbd_ioreq.d ./Core/Src/usbd_ioreq.o ./Core/Src/usbd_ioreq.su ./Core/Src/usbd_midi.d ./Core/Src/usbd_midi.o ./Core/Src/usbd_midi.su ./Core/Src/wavetable.d ./Core/Src/wavetable.o ./Core/Src/wavetable.su

.PHONY: clean-Core-2f-Src

