
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_camera_hw.h"
#include <cust_gpio_usage.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/xlog.h>
#include <linux/version.h>
#ifdef CONFIG_COMPAT
#include <linux/fs.h>
#include <linux/compat.h>
#endif
#include "kd_flashlight.h"
#include <cust_leds_def.h>

/******************************************************************************
 * Debug configuration
******************************************************************************/
// availible parameter
// ANDROID_LOG_ASSERT
// ANDROID_LOG_ERROR
// ANDROID_LOG_WARNING
// ANDROID_LOG_INFO
// ANDROID_LOG_DEBUG
// ANDROID_LOG_VERBOSE
#define TAG_NAME "[sub_strobe.c]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
//#define PK_DBG_FUNC(fmt, arg...)    pr_debug(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_DBG_FUNC(fmt, arg...)    xlog_printk(ANDROID_LOG_DEBUG  , TAG_NAME, KERN_INFO  "%s: " fmt, __FUNCTION__ ,##arg)

#define PK_WARN(fmt, arg...)        pr_warning(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_NOTICE(fmt, arg...)      pr_notice(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_INFO(fmt, arg...)        pr_info(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)
#define PK_TRC_FUNC(f)              pr_debug(TAG_NAME "<%s>\n", __FUNCTION__)
#define PK_TRC_VERBOSE(fmt, arg...) pr_debug(TAG_NAME fmt, ##arg)
#define PK_ERROR(fmt, arg...)       pr_err(TAG_NAME "%s: " fmt, __FUNCTION__ ,##arg)

#define DEBUG_LEDS_STROBE
#ifdef  DEBUG_LEDS_STROBE
	#define PK_DBG PK_DBG_FUNC
	#define PK_VER PK_TRC_VERBOSE
	#define PK_ERR PK_ERROR
#else
	#define PK_DBG(a,...)
	#define PK_VER(a,...)
	#define PK_ERR(a,...)
#endif
#define LEDS_CUSTOM_MODE_THRES 	20
static DEFINE_SPINLOCK(g_strobeSMPLock); /* cotta-- SMP proection */

static int g_timeOutTimeMs=0;
static BOOL g_is_torch_mode = 0;
static U16 g_duty = 0;
static struct work_struct workTimeOut;
static struct hrtimer g_timeOutTimer;
static u32 strobe_Res = 0;
static u32 strobe_Timeus = 0;
static BOOL g_strobe_On = 0;
static int duty_map[] = {40,32,20,24,48,64,80,96};

extern int mt_brightness_set_pmic(enum mt65xx_led_pmic pmic_type, u32 level, u32 div);
int FL_disable_sub(void)
{
	PK_DBG("FL_disable_sub");
    //sgm3785_shutdoff();
//	upmu_set_flash_en(0);
	//upmu_set_rg_bst_drv_1m_ck_pdn(1);
	
    mt_brightness_set_pmic(MT65XX_LED_PMIC_NLED_ISINK_SUBFLASH,0,1);
    return 0;
}

static enum hrtimer_restart ledTimeOutCallback(struct hrtimer *timer)
{
    schedule_work(&workTimeOut);
    return HRTIMER_NORESTART;
}

static void work_timeOutFunc(struct work_struct *data)
{
    FL_disable_sub();
    PK_DBG("ledTimeOut_callback\n");
    //printk(KERN_ALERT "work handler function./n");
}

static void timerInit(void)
{
  INIT_WORK(&workTimeOut, work_timeOutFunc);
	g_timeOutTimeMs=1000; //1s
	hrtimer_init( &g_timeOutTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	g_timeOutTimer.function=ledTimeOutCallback;

}


int FL_enable_sub(void)
{
	PK_DBG("wangyang FL_enable_sub_is_torch_mode%d\n",g_is_torch_mode);	
	if(g_is_torch_mode)
	{
		PK_DBG("wangyang go here.g_is_torch_mode=1 g_duty = %d \n",g_duty);
		//sgm3785_set_flash_mode(g_duty);
		
		mt_brightness_set_pmic(MT65XX_LED_PMIC_NLED_ISINK_SUBFLASH,g_duty,1);

		g_is_torch_mode=0;
	}
	else
	{	
			mt_brightness_set_pmic(MT65XX_LED_PMIC_NLED_ISINK_SUBFLASH,g_duty,1);
	}
	
//	upmu_set_rg_bst_drv_1m_ck_pdn(0);
//	upmu_set_flash_en(1);
    return 0;
}


int FL_dim_duty_sub(kal_uint32 duty)
{
	PK_DBG("wangyang FL_dim_duty_sub %d, g_is_torch_mode %d \n", duty, g_is_torch_mode);
	if(duty <= 1)	{
		g_is_torch_mode = 1;
		g_duty = duty_map[duty];
	}
	else{
		g_is_torch_mode = 0;	
		g_duty = duty_map[duty];
	}
	if((g_timeOutTimeMs == 0) && (duty > 1))
	{
	    
		g_duty = duty_map[duty];
		PK_ERR("FL_dim_duty_sub %d > thres %d, FLASH mode but timeout %d \n", duty, LEDS_CUSTOM_MODE_THRES, g_timeOutTimeMs);	
		g_is_torch_mode = 0;	
	}	
//	upmu_set_flash_dim_duty(duty);
    return 0;
}

int FL_step_sub(kal_uint32 step)
{
	int sTab[8]={0,2,4,6,9,11,13,15};
	PK_DBG("FL_step_sub");
//	upmu_set_flash_sel(sTab[step]);	
    return 0;
}

int FL_init_sub(void)
{
//	upmu_set_flash_dim_duty(0);
//	upmu_set_flash_sel(0);
	PK_DBG("FL_init");
	FL_disable_sub();
	INIT_WORK(&workTimeOut, work_timeOutFunc);
	g_is_torch_mode = 0;
    return 0;
}

int FL_uninit_sub(void)
{
	PK_DBG("FL_uninit_sub");
	FL_disable_sub();
	g_is_torch_mode = 0;
    return 0;
}

/*****************************************************************************
User interface
*****************************************************************************/


static int sub_strobe_ioctl(unsigned int cmd, unsigned long arg)
{
    int i4RetValue = 0;
    int ior_shift;
    int iow_shift;
    int iowr_shift;
    int iFlashType = (int)FLASHLIGHT_NONE;
    ior_shift = cmd - (_IOR(FLASHLIGHT_MAGIC,0, int));
    iow_shift = cmd - (_IOW(FLASHLIGHT_MAGIC,0, int));
    iowr_shift = cmd - (_IOWR(FLASHLIGHT_MAGIC,0, int));
    PK_DBG("sub_strobe_ioctl() line=%d ior_shift=%d, iow_shift=%d iowr_shift=%d arg=%d\n",__LINE__, ior_shift, iow_shift, iowr_shift, arg);
    switch(cmd)
    {

        case FLASH_IOC_SET_TIME_OUT_TIME_MS:
            PK_DBG("sub_strobe_ioctl_TIME_OUT_TIME_MS: %d\n",arg);
            g_timeOutTimeMs=arg;
        break;


        case FLASH_IOC_SET_DUTY :
            PK_DBG("sub_strobe_ioctl_DUTY: %d\n",arg);
            FL_dim_duty_sub(arg);
            break;


        case FLASH_IOC_SET_STEP:
            PK_DBG("sub_strobe_ioctl_STEP: %d\n",arg);

            break;

        case FLASH_IOC_SET_ONOFF :
            PK_DBG("sub_strobe_ioctlF: %d\n",arg);
            if(arg==1)
            {
                if(g_timeOutTimeMs!=0)
                {
                    ktime_t ktime;
                    ktime = ktime_set( 0, g_timeOutTimeMs*1000000 );
                    hrtimer_start( &g_timeOutTimer, ktime, HRTIMER_MODE_REL );
                }
                FL_enable_sub();
            }
            else
            {
                FL_disable_sub();
                hrtimer_cancel( &g_timeOutTimer );
            }
            break;
        //case FLASHLIGHTIOC_G_FLASHTYPE:
        //    iFlashType = FLASHLIGHT_LED_CONSTANT;
         //   if(copy_to_user((void __user *) arg , (void*)&iFlashType , _IOC_SIZE(cmd)))
         //   {
         //       PK_DBG("[strobe_ioctl] ioctl copy to user failed\n");
         //       return -EFAULT;
         //   }
         //   break;            
        default :
            PK_DBG(" No such command \n");
            i4RetValue = -EPERM;
            break;
    }
    return i4RetValue;
}


static int sub_strobe_open(void *pArg)
{
    int i4RetValue = 0;
    PK_DBG("sub_strobe_open line=%d\n", __LINE__);

    if (0 == strobe_Res)
    {
        FL_init_sub();
        timerInit();
    }
    PK_DBG("sub_strobe_open line=%d\n", __LINE__);
    spin_lock_irq(&g_strobeSMPLock);


    if(strobe_Res)
    {
        PK_ERR(" busy!\n");
        i4RetValue = -EBUSY;
    }
    else
    {
        strobe_Res += 1;
    }


    spin_unlock_irq(&g_strobeSMPLock);
    PK_DBG("sub_strobe_open=%d\n", __LINE__);

    return i4RetValue;

}


static int sub_strobe_release(void *pArg)
{
    PK_DBG(" sub_strobe_release\n");

    if (strobe_Res)
    {
        spin_lock_irq(&g_strobeSMPLock);

        strobe_Res = 0;
        strobe_Timeus = 0;

        /* LED On Status */
        g_strobe_On = FALSE;

        spin_unlock_irq(&g_strobeSMPLock);

        FL_uninit_sub();
    }

    PK_DBG(" Done\n");

    return 0;

}


FLASHLIGHT_FUNCTION_STRUCT	subStrobeFunc=
{
	sub_strobe_open,
	sub_strobe_release,
	sub_strobe_ioctl
};


MUINT32 subStrobeInit(PFLASHLIGHT_FUNCTION_STRUCT *pfFunc)
{
    if (pfFunc != NULL)
    {
        *pfFunc = &subStrobeFunc;
    }
    return 0;
}





