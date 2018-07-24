#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
#include <mach/mt_pm_ldo.h>
#include <mach/mt_gpio.h>
#include <linux/delay.h>
#include <sgm3785_drv.h>

static kal_bool active_flag = KAL_FALSE;

struct pwm_spec_config sgm3785_config = {
    .pwm_no = PWM_NO,
    .mode = PWM_MODE_OLD,
    .clk_div = CLK_DIV32,//CLK_DIV16: 147.7kHz    CLK_DIV32: 73.8kHz
    .clk_src = PWM_CLK_OLD_MODE_BLOCK,
    .pmic_pad = false,
    .PWM_MODE_OLD_REGS.IDLE_VALUE = IDLE_FALSE,
    .PWM_MODE_OLD_REGS.GUARD_VALUE = GUARD_FALSE,
    .PWM_MODE_OLD_REGS.GDURATION = 0,
    .PWM_MODE_OLD_REGS.WAVE_NUM = 0,
    .PWM_MODE_OLD_REGS.DATA_WIDTH = 11,
    .PWM_MODE_OLD_REGS.THRESH = 5,
};

//*****************sys file system start*****************//

/*flash mode
ENF: 1                           _____________
         0 ____________|       660ms       |____
ENM:1    _      _     _     _      _     _     _      _     _
         0 _| |_| |_| |_| |_| |_| |_| |_| |_| |_
*/
S32 sgm3785_set_flash_mode(U16 duty)
{	
	S32 ret;
	sgm3138_dbg("sgm3785_set_flash_mode: active_flag = %d,duty = %d",active_flag,duty);
	if(active_flag == KAL_TRUE)
		return;
	if(mt_set_gpio_mode(ENF,GPIO_MODE_00)){sgm3138_dbg("[sgm3785] set enf gpio mode failed!! \n");}
	if(mt_set_gpio_dir(ENF,GPIO_DIR_OUT)){sgm3138_dbg("[sgm3785] set  enf gpio dir failed!! \n");}
	mt_set_gpio_out(ENF,GPIO_OUT_ZERO);
    mdelay(2);
	if(mt_set_gpio_mode(ENM,GPIO_MODE_05)){sgm3138_dbg("[sgm3785] set enf gpio mode failed!! \n");}
	sgm3785_config.PWM_MODE_OLD_REGS.THRESH = duty;
	ret = pwm_set_spec_config(&sgm3785_config);	
    mdelay(2);
	mt_set_gpio_out(ENF,GPIO_OUT_ONE);	

	active_flag = KAL_TRUE;
	return ret;
}

/*movie/torch mode
ENF: 1
          0 _____________________________________
ENM:1	 ______	     _     _      _     _      _	
         0 _| >5ms |_| |_| |_| |_| |_| |________
*/	
S32 sgm3785_set_torch_mode(U16 duty)
{
	S32 ret;
	sgm3138_dbg("sgm3785_set_torch_mode: active_flag = %d,duty = %d",active_flag,duty);

	if(active_flag == KAL_TRUE)
		return;
	if(mt_set_gpio_mode(ENF,GPIO_MODE_00)){sgm3138_dbg("[sgm3785] set enf gpio mode failed!! \n");}
	if(mt_set_gpio_dir(ENF,GPIO_DIR_OUT)){sgm3138_dbg("[sgm3785] set  enf gpio dir failed!! \n");}
	mt_set_gpio_out(ENF,GPIO_OUT_ZERO);
	mdelay(2);

	if(mt_set_gpio_mode(ENM,GPIO_MODE_00)){sgm3138_dbg("[sgm3785] set enf gpio mode failed!! \n");}
	if(mt_set_gpio_dir(ENM,GPIO_DIR_OUT)){sgm3138_dbg("[sgm3785] set  enf gpio dir failed!! \n");}
	mt_set_gpio_out(ENM,GPIO_OUT_ZERO);
	mdelay(1);
	mt_set_gpio_out(ENM,GPIO_OUT_ONE);
	mdelay(6);

	if(mt_set_gpio_mode(ENM,GPIO_MODE_05)){sgm3138_dbg("[sgm3785] set enf gpio mode failed!! \n");}
	sgm3785_config.PWM_MODE_OLD_REGS.THRESH = duty;
	ret = pwm_set_spec_config(&sgm3785_config);

	active_flag = KAL_TRUE;
	return ret;
}

S32 sgm3785_shutdoff(void)
{
	if(active_flag == KAL_FALSE)
		return;

	mt_pwm_disable(PWM_NO, false);

	if(mt_set_gpio_mode(ENF,GPIO_MODE_00)){sgm3138_dbg("[sgm3785] set enf gpio mode failed!! \n");}
	if(mt_set_gpio_mode(ENM,GPIO_MODE_00)){sgm3138_dbg("[sgm3785] set enf gpio mode failed!! \n");}
	if(mt_set_gpio_dir(ENF,GPIO_DIR_OUT)){sgm3138_dbg("[sgm3785] set  enf gpio dir failed!! \n");}
	if(mt_set_gpio_dir(ENM,GPIO_DIR_OUT)){sgm3138_dbg("[sgm3785] set  enf gpio dir failed!! \n");}
	mt_set_gpio_out(ENF,GPIO_OUT_ZERO);
	mt_set_gpio_out(ENM,GPIO_OUT_ZERO);
	mdelay(5);

	active_flag = KAL_FALSE;
}

int sgm3785_ioctr(U16 mode, U16 duty)
{
	sgm3138_dbg("[sgm3785] mode %d , duty %d\n", mode, duty);

	if(mode < MODE_MIN || mode >= MODE_MAX)
	{
		sgm3138_dbg("[sgm3785] mode error!!!\n");
		return 1;
	}
	
	if(duty < 0 || duty > 20)
	{
		sgm3138_dbg("[sgm3785] duty error!!!\n");
		return 1;
	}

	switch(mode){
		case FLASH_M:
			sgm3785_set_flash_mode(duty);
			break;

		case TORCH_M:
			sgm3785_set_torch_mode(duty);
			break;

		case PWD_M:
			sgm3785_shutdoff();
			break;

		default:
			sgm3138_dbg("[sgm3785] error ioctr!!!\n");
			break;
	}

	return 0;
}

void sgm3785_FL_Enable(int duty)
{
	if(duty > 0)//flashlight mode
		sgm3785_ioctr(FLASH_M, F_DUTY);
	else //torch mode
		sgm3785_ioctr(TORCH_M, T_DUTY);
}

void sgm3785_FL_Disable(void)
{
	sgm3785_ioctr(PWD_M, 0);
}

static ssize_t show_sgm3785_torch_mode(struct device *dev,struct device_attribute *attr, char *buf)
{
	  return  sprintf(buf, "show_sgm3785_torch_mode\n");
}
static ssize_t restore_sgm3785_torch_mode(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
    {
        kal_int32 temp = 0;
        
        sscanf(buf, "%d", &temp);
        sgm3138_dbg("[sgm3785] restore_sgm3785_torch_mode temp = %d\n",temp);
        if(temp < 0 || temp > 11)
        {
            
            sgm3785_shutdoff();
            sgm3138_dbg("[sgm3785] duty error!!!\n");
            //return 1;
        }
        else
        {
            sgm3785_set_torch_mode(temp);
        }
        
        return size;
    }


static ssize_t restore_sgm3785_flash_mode(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	kal_int32 temp = 0;
	

	sscanf(buf, "%d", &temp);
	
    sgm3138_dbg("[sgm3785] restore_sgm3785_flash_mode temp = %d\n",temp);
	

	if(temp < 0 || temp > 11)
	{
        sgm3785_shutdoff();
		sgm3138_dbg("[sgm3785] duty error!!!\n");
		//return 1;
	}
    else
    {
        sgm3785_set_flash_mode(temp);
    }
    
    return size;
}

static ssize_t show_sgm3785_flash_mode(struct device *dev,struct device_attribute *attr, char *buf)
{
      return  sprintf(buf, "show_sgm3785_flash_mode\n");
}


static DEVICE_ATTR(sgm3785_torch_mode,  0664, show_sgm3785_torch_mode, restore_sgm3785_torch_mode);
static DEVICE_ATTR(sgm3785_flash_mode,  0664, show_sgm3785_flash_mode, restore_sgm3785_flash_mode);
//*****************sys file system end*****************//
static void hw_init()
{
 	mt_set_gpio_mode(GPIO_CAMERA_FLASH_MODE_PIN,GPIO_CAMERA_FLASH_MODE_PIN_M_GPIO);
 	mt_set_gpio_dir(GPIO_CAMERA_FLASH_MODE_PIN,GPIO_DIR_OUT);
 	
 	mt_set_gpio_mode(GPIO_CAMERA_FLASH_EN_PIN,GPIO_CAMERA_FLASH_EN_PIN_M_GPIO);
 	mt_set_gpio_dir(GPIO_CAMERA_FLASH_EN_PIN,GPIO_DIR_OUT);
}


static int sgm3785_prob(struct platform_device *dev)
{ 
  int ret_device_file=0;
  sgm3138_dbg("isgm3785_prob");
  hw_init();
  ret_device_file = device_create_file(&(dev->dev), &dev_attr_sgm3785_flash_mode);
  ret_device_file = device_create_file(&(dev->dev), &dev_attr_sgm3785_torch_mode);
  return 0;
}

static int sgm3785_remove(struct platform_device *dev)    
{
    sgm3138_dbg("sgm3785_remove");
    return 0;
}

static void sgm3785_shutdown(struct platform_device *dev)    
{
     sgm3138_dbg("sgm3785_shutdown");
     return 0;
}

static int sgm3785_suspend(struct platform_device *dev, pm_message_t state)    
{
    sgm3138_dbg("sgm3785_suspend");   
    return 0;
}

static int sgm3785_resume(struct platform_device *dev)
{
   sgm3138_dbg("sgm3785_resume"); 
   return 0;
}

struct platform_device flash_leds_sgm3785_device = {
    .name   = "sgm3785",
    .id	    = -1,
};

static struct platform_driver flash_leds_sgm3785_driver = {
    .probe       = sgm3785_prob,
    .remove      = sgm3785_remove,
    .shutdown    = sgm3785_shutdown,
    .suspend     = sgm3785_suspend,
    .resume      = sgm3785_resume,
    .driver      = {
        .name = "sgm3785",
    },
};

static void flash_leds_sgm3785_init(void)
{
    int ret;
    ret = platform_device_register(&flash_leds_sgm3785_device);
    if (ret) {
    printk("<<-sgm3785->> Unable to register device (%d)\n", ret);
	return ret;
    }
    
    ret = platform_driver_register(&flash_leds_sgm3785_driver);
    if (ret) {
    printk("<<-sgm3785->> Unable to register driver (%d)\n", ret);
	return ret;
    }
    return 0;    
}

static void flash_leds_sgm3785_exit(void)
{
	sgm3138_dbg("flash_leds_sgm3785_exit");
	platform_device_unregister(&flash_leds_sgm3785_device);
	platform_driver_unregister(&flash_leds_sgm3785_driver);

}


fs_initcall(flash_leds_sgm3785_init);
module_exit(flash_leds_sgm3785_exit);

MODULE_AUTHOR("shenwuyi");
MODULE_DESCRIPTION("Flash Led Sgm3785 Device Driver");
MODULE_LICENSE("GPL");
