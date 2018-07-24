/************************************************************************************************
*							*file name : sgm3785_drv.h
*							*Version : v1.0
*							*Author : erick
*							*Date : 2015.4.16
*************************************************************************************************/
#ifndef _SGM3785_DRV_H_
#define _SGM3785_DRV_H_
#include <linux/delay.h>
#include <linux/xlog.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_pwm.h>
#include <mach/mt_pwm_hal.h>
#include <mach/mt_gpio.h>
#include <cust_gpio_usage.h>


#define PWM_NO PWM_MIN//PWM4   	//HWpwm0 --> SWpwm1
#define F_DUTY 5   		// 0,1,2,3,4,5,6,7,8,9,10
#define T_DUTY 5   		// 0,1,2,3,4,5,6,7,8,9,10

#ifndef GPIO_CAMERA_FLASH_EN_PIN 
#define GPIO_CAMERA_FLASH_EN_PIN (GPIO78 | 0x80000000)
#endif

#ifndef GPIO_CAMERA_FLASH_EN_PIN_M_GPIO 
#define GPIO_CAMERA_FLASH_EN_PIN_M_GPIO GPIO_MODE_00
#endif

#ifndef GPIO_CAMERA_FLASH_MODE_PIN 
#define GPIO_CAMERA_FLASH_MODE_PIN (GPIO80 | 0x80000000)
#endif

#ifndef GPIO_CAMERA_FLASH_MODE_PIN_M_GPIO 
#define GPIO_CAMERA_FLASH_MODE_PIN_M_GPIO GPIO_MODE_00
#endif


#define ENF	GPIO_CAMERA_FLASH_MODE_PIN
#define ENM	GPIO_CAMERA_FLASH_EN_PIN

#define SGM3138_DEBUG
#ifdef SGM3138_DEBUG
#define sgm3138_dbg printk
#else
#define sgm3138_dbg //
#endif

enum SGM3785_MODE{
	MODE_MIN = 0,
	PWD_M = MODE_MIN,
	FLASH_M,
	TORCH_M,
	MODE_MAX
};

#endif
