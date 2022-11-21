################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../main.c \
../mbus-decode.c \
../mbus.c \
../verbose.c 

OBJS += \
./main.o \
./mbus-decode.o \
./mbus.o \
./verbose.o 

C_DEPS += \
./main.d \
./mbus-decode.d \
./mbus.d \
./verbose.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


