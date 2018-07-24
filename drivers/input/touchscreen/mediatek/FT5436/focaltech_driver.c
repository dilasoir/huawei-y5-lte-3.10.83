/*
 * This software is licensed under the terms of the GNU General Public 
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms. 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details. 
 * * VERSION      	DATE			AUTHOR          Note
 *    1.0		  2013-7-16			Focaltech        initial  based on MTK platform
 * 
 */
 
//////////////////////////////////////

#include "tpd.h"
#include <cust_alsps.h>

#include "tpd_custom_fts.h"
#ifdef FTS_CTL_IIC
#include "focaltech_ctl.h"
#endif
#ifdef TPD_SYSFS_DEBUG
#include "focaltech_ex_fun.h"
#endif
#ifdef TPD_PROXIMITY
#include <linux/hwmsensor.h>
#include <linux/hwmsen_dev.h>
#include <linux/sensors_io.h>
#endif

#include "cust_gpio_usage.h"



#include <linux/proc_fs.h>  /*proc*/
#include <linux/input/mt.h>  /*proc*/
#include <mach/hardwareinfo.h>
extern hardware_info_struct hardware_info;

#define TP_MONITOR_CTL  //julian 5.16
//#define FTS_SCAP_TEST


//wangcq327 --- add start
static int TP_VOL;
//wangcq327 --- add end
  struct Upgrade_Info fts_updateinfo[] =
{
	{0x54,"FT5x46",TPD_MAX_POINTS_5,AUTO_CLB_NONEED,2, 2, 0x54, 0x2c, 10, 2000},
	{0x36,"FT6x36",TPD_MAX_POINTS_2,AUTO_CLB_NONEED,100, 30, 0x79, 0x18, 10, 2000},//CHIP ID error
    {0x55,"FT5x06",TPD_MAX_POINTS_5,AUTO_CLB_NEED,50, 30, 0x79, 0x03, 1, 2000},
    {0x08,"FT5606",TPD_MAX_POINTS_5,AUTO_CLB_NEED,50, 30, 0x79, 0x06, 100, 2000},
	{0x0a,"FT5x16",TPD_MAX_POINTS_5,AUTO_CLB_NEED,50, 30, 0x79, 0x07, 1, 1500},
	{0x05,"FT6208",TPD_MAX_POINTS_2,AUTO_CLB_NONEED,60, 30, 0x79, 0x05, 10, 2000},
	{0x06,"FT6x06",TPD_MAX_POINTS_2,AUTO_CLB_NONEED,100, 30, 0x79, 0x08, 10, 2000},
	{0x55,"FT5x06i",TPD_MAX_POINTS_5,AUTO_CLB_NEED,50, 30, 0x79, 0x03, 1, 2000},
	{0x14,"FT5336",TPD_MAX_POINTS_5,AUTO_CLB_NONEED,30, 30, 0x79, 0x11, 10, 2000},
	{0x13,"FT3316",TPD_MAX_POINTS_5,AUTO_CLB_NONEED,30, 30, 0x79, 0x11, 10, 2000},
	{0x12,"FT5436i",TPD_MAX_POINTS_5,AUTO_CLB_NONEED,30, 30, 0x79, 0x11, 10, 2000},
	{0x11,"FT5336i",TPD_MAX_POINTS_5,AUTO_CLB_NONEED,30, 30, 0x79, 0x11, 10, 2000},
};
				
struct Upgrade_Info fts_updateinfo_curr;

#ifdef TPD_PROXIMITY
#define APS_ERR(fmt,arg...)           	printk("<<proximity>> "fmt"\n",##arg)
#define TPD_PROXIMITY_DEBUG(fmt,arg...) printk("<<proximity>> "fmt"\n",##arg)
#define TPD_PROXIMITY_DMESG(fmt,arg...) printk("<<proximity>> "fmt"\n",##arg)

static u8 tpd_proximity_flag 			= 0;
static u8 tpd_proximity_suspend 		= 0;
static u8 tpd_proximity_detect 		= 1;//0-->close ; 1--> far away
static u8 tpd_proximity_detect_prev	= 0xff;//0-->close ; 1--> far away
#endif

#ifdef FTS_GESTRUE
short pointnum = 0;

unsigned short coordinate_x[255] = {0};
unsigned short coordinate_y[255] = {0};
static int ft_wakeup_gesture = 0;
static long huawei_gesture = 0;
static int double_gesture = 0;
static int draw_gesture = 0;
static int gesture_echo[12]={0};
#define GT9XX_ECHO_PROC_FILE     "gesture_echo"
 
static ssize_t ft5436echo_read_proc(struct file *file, char __user *page, size_t size, loff_t *ppos);
static ssize_t ft5436echo_write_proc(struct file *, const char __user *, size_t, loff_t *);
/*
static struct proc_dir_entry *ft5436_echo_proc = NULL;

static const struct file_operations configecho_proc_ops = {
    .owner = THIS_MODULE,
    .read = ft5436echo_read_proc,
    .write = ft5436echo_write_proc,
};
*/
#endif
extern struct tpd_device *tpd;
 
static struct i2c_client *i2c_client = NULL;
struct task_struct *thread = NULL;
 
static DECLARE_WAIT_QUEUE_HEAD(waiter);
static DEFINE_MUTEX(i2c_access);
 
 
static void tpd_eint_interrupt_handler(void);
extern void mt_eint_unmask(unsigned int line);
extern void mt_eint_mask(unsigned int line);
extern void mt_eint_set_hw_debounce(unsigned int eint_num, unsigned int ms);
extern unsigned int mt_eint_set_sens(unsigned int eint_num, unsigned int sens);
//extern void mt_eint_registration(unsigned int eint_num, unsigned int is_deb_en, unsigned int pol, void (EINT_FUNC_PTR)(void), unsigned int is_auto_umask);
 
static int tpd_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tpd_detect (struct i2c_client *client, struct i2c_board_info *info);
static int tpd_remove(struct i2c_client *client);
static int touch_event_handler(void *unused);
#ifdef USB_CHARGE_DETECT
static int b_usb_plugin = 0;
static int ctp_is_probe = 0; 
#endif

static int tpd_flag 					= 0;
static int tpd_halt						= 0;
static int point_num 					= 0;
static int p_point_num 					= 0;

int up_flag=0,up_count=0;

//#define TPD_CLOSE_POWER_IN_SLEEP
#define TPD_OK 							0
//register define
#define DEVICE_MODE 					0x00
#define GEST_ID 						0x01
#define TD_STATUS 						0x02
//point1 info from 0x03~0x08
//point2 info from 0x09~0x0E
//point3 info from 0x0F~0x14
//point4 info from 0x15~0x1A
//point5 info from 0x1B~0x20
//register define

#define TPD_RESET_ISSUE_WORKAROUND

#define TPD_MAX_RESET_COUNT 			2

struct ts_event 
{
	u16 au16_x[CFG_MAX_TOUCH_POINTS];				/*x coordinate */
	u16 au16_y[CFG_MAX_TOUCH_POINTS];				/*y coordinate */
	u8 au8_touch_event[CFG_MAX_TOUCH_POINTS];		/*touch event: 0 -- down; 1-- up; 2 -- contact */
	u8 au8_finger_id[CFG_MAX_TOUCH_POINTS];			/*touch ID */
	u16 pressure[CFG_MAX_TOUCH_POINTS];
	u16 area[CFG_MAX_TOUCH_POINTS];
	u8 touch_point;
	int touchs;
	u8 touch_point_num;
};


#ifdef TPD_HAVE_BUTTON 
static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
#endif
#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
static int tpd_wb_start_local[TPD_WARP_CNT] = TPD_WARP_START;
static int tpd_wb_end_local[TPD_WARP_CNT]   = TPD_WARP_END;
#endif
#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
static int tpd_calmat_local[8]     = TPD_CALIBRATION_MATRIX;
static int tpd_def_calmat_local[8] = TPD_CALIBRATION_MATRIX;
#endif

#define VELOCITY_CUSTOM_FT5206
#ifdef VELOCITY_CUSTOM_FT5206
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>

// for magnify velocity********************************************

#ifndef TPD_VELOCITY_CUSTOM_X
#define TPD_VELOCITY_CUSTOM_X 			10
#endif
#ifndef TPD_VELOCITY_CUSTOM_Y
#define TPD_VELOCITY_CUSTOM_Y 			10
#endif

#define TOUCH_IOC_MAGIC 				'A'

#define TPD_GET_VELOCITY_CUSTOM_X _IO(TOUCH_IOC_MAGIC,0)
#define TPD_GET_VELOCITY_CUSTOM_Y _IO(TOUCH_IOC_MAGIC,1)


static int g_v_magnify_x =TPD_VELOCITY_CUSTOM_X;
static int g_v_magnify_y =TPD_VELOCITY_CUSTOM_Y;

static int tpd_misc_open(struct inode *inode, struct file *file)
{
/*
	file->private_data = adxl345_i2c_client;

	if(file->private_data == NULL)
	{
		printk("tpd: null pointer!!\n");
		return -EINVAL;
	}
	*/
	return nonseekable_open(inode, file);
}
/*----------------------------------------------------------------------------*/
static int tpd_misc_release(struct inode *inode, struct file *file)
{
	//file->private_data = NULL;
	return 0;
}
/*----------------------------------------------------------------------------*/
//static int adxl345_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
//       unsigned long arg)
static long tpd_unlocked_ioctl(struct file *file, unsigned int cmd,
       unsigned long arg)
{
	//struct i2c_client *client = (struct i2c_client*)file->private_data;
	//struct adxl345_i2c_data *obj = (struct adxl345_i2c_data*)i2c_get_clientdata(client);	
	//char strbuf[256];
	void __user *data;
	
	long err = 0;
	
	if(_IOC_DIR(cmd) & _IOC_READ)
	{
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	}
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
	{
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}

	if(err)
	{
		printk("tpd: access error: %08X, (%2d, %2d)\n", cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch(cmd)
	{
		case TPD_GET_VELOCITY_CUSTOM_X:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}			
			
			if(copy_to_user(data, &g_v_magnify_x, sizeof(g_v_magnify_x)))
			{
				err = -EFAULT;
				break;
			}				 
			break;

	   case TPD_GET_VELOCITY_CUSTOM_Y:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}			
			
			if(copy_to_user(data, &g_v_magnify_y, sizeof(g_v_magnify_y)))
			{
				err = -EFAULT;
				break;
			}				 
			break;

		default:
			printk("tpd: unknown IOCTL: 0x%08x\n", cmd);
			err = -ENOIOCTLCMD;
			break;
	}

	return err;
}


static struct file_operations tpd_fops = {
//	.owner = THIS_MODULE,
	.open = tpd_misc_open,
	.release = tpd_misc_release,
	.unlocked_ioctl = tpd_unlocked_ioctl,
};
/*----------------------------------------------------------------------------*/
static struct miscdevice tpd_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = TPD_NAME,
	.fops = &tpd_fops,
};

//**********************************************
#endif

struct touch_info {
    int y[10];
    int x[10];
    int p[10];
    int id[10];
};
 
static const struct i2c_device_id ft5206_tpd_id[] = {{TPD_NAME,0},{}};
//unsigned short force[] = {0,0x70,I2C_CLIENT_END,I2C_CLIENT_END}; 
//static const unsigned short * const forces[] = { force, NULL };
//static struct i2c_client_address_data addr_data = { .forces = forces, };
static struct i2c_board_info __initdata ft5206_i2c_tpd={ I2C_BOARD_INFO(TPD_NAME, (0x70>>1))};
 
static struct i2c_driver tpd_i2c_driver = {
  	.driver = {
	 	.name 	= TPD_NAME,
	//	.owner 	= THIS_MODULE,
  	},
  	.probe 		= tpd_probe,
  	.remove 	= tpd_remove,
  	.id_table 	= ft5206_tpd_id,
  	.detect 	= tpd_detect,
// 	.shutdown	= tpd_shutdown,
//  .address_data = &addr_data,
};

#ifdef USB_CHARGE_DETECT
void tpd_usb_plugin(int plugin)
{
	int ret = -1;
	b_usb_plugin = plugin;
	
	if(!ctp_is_probe)
	{
		return;
	}
	printk("Fts usb detect: %s %d %d.\n",__func__,b_usb_plugin,tpd_halt);
	if ( tpd_halt ) return;
	ret = i2c_smbus_write_i2c_block_data(i2c_client, 0x8B, 1, &b_usb_plugin);
	if ( ret < 0 )
	{
		printk("Fts usb detect write err: %s %d.\n",__func__,b_usb_plugin);
	}
}
EXPORT_SYMBOL(tpd_usb_plugin);
#endif




//*******************HQ  proc info*****************//
#define HQ_DBG_CTP_INFO
#ifdef HQ_DBG_CTP_INFO
extern int fts_get_fw_ver(struct i2c_client *client);

extern int fts_get_vendor_id(struct i2c_client *client);

static ssize_t proc_get_ctp_firmware_id(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
     char *page = NULL;
    char *ptr = NULL;
    int fw_ver,vid;
    int i, len, err = -1;

	  page = kmalloc(PAGE_SIZE, GFP_KERNEL);	
	  if (!page) 
	  {		
		kfree(page);		
		return -ENOMEM;	
	  }

    ptr = page; 
	if (*ppos)  // CMD call again
	{
		return 0;
	}
    /* Touch PID & VID */
    ptr += sprintf(ptr, "==== FT5436 Version ID ====\n");
    fw_ver=fts_get_fw_ver(i2c_client);
    vid=fts_get_vendor_id(i2c_client);
    ptr += sprintf(ptr, "Chip Vendor ID: 0x%x  FW_VERSION: 0x%x\n", vid,fw_ver);
    len = ptr - page; 			 	
    if(*ppos >= len)
	  {		
		  kfree(page); 		
		  return 0; 	
	  }	
	  err = copy_to_user(buffer,(char *)page,len); 			
	  *ppos += len; 	
	  if(err) 
	  {		
	    kfree(page); 		
		  return err; 	
	  }	
	  kfree(page); 	
	  return len;	
	
}

// used for gesture function switch
#ifdef FTS_GESTRUE
static ssize_t gesture_switch_read_proc(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
	char *ptr = page;
	int i;
	if (*ppos)  // CMD call again
		return 0;

	ptr += sprintf(ptr, "%d\n", ft_wakeup_gesture);

	*ppos += ptr - page;
	return (ptr - page);
}

static ssize_t gesture_switch_write_proc(struct file *file, char __user *buff, size_t size, loff_t *ppos)
{
	char wtire_data[32] = {0};

	if (size >= 32)
		return -EFAULT;

	if (copy_from_user( &wtire_data, buff, size ))
		return -EFAULT;

	if (wtire_data[0] == '1')
		ft_wakeup_gesture = 1;
	else
		ft_wakeup_gesture = 0;

	printk("zjy: %s, wakeup_gesture = %d\n", __func__, ft_wakeup_gesture);

	return size;
}
#if defined(FTS_HUAWEI_DESIGN)

static ssize_t ft5436echo_read_proc(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
	int len = 0;
	char *p = page;
	
    if (*ppos)  // CMD call again
    {
        return 0;
    }

	int i = 0;
	for(i=0;i<=11;i++)
	{
		p += sprintf(p, "%04x", gesture_echo[i]);
	}
	*ppos += p - page;

        memset(gesture_echo,0,sizeof(gesture_echo));

	return (p-page);
}

static ssize_t ft5436echo_write_proc(struct file *filp, const char __user *buffer, size_t count, loff_t *off)
{
 
}

static ssize_t huawei_gesture_switch_read_proc(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
	char *ptr = page;
	int i;
	if (*ppos)  // CMD call again
		return 0;

	ptr += sprintf(ptr, "%ld\n", huawei_gesture);

	*ppos += ptr - page;
	return (ptr - page);
}

static ssize_t huawei_gesture_switch_write_proc(struct file *file, char __user *buff, size_t size, loff_t *ppos)
{
	char wtire_data[32] = {0};

	if (size >= 32)
		return -EFAULT;

	if (copy_from_user( &wtire_data, buff, size ))
		return -EFAULT;
	huawei_gesture= simple_strtoul(wtire_data,NULL,10);

	if((huawei_gesture & 1) || (huawei_gesture & 0x2000)){
		ft_wakeup_gesture = 1;
	}else{
		ft_wakeup_gesture = 0;
	}
	
	if(huawei_gesture & 1)
		double_gesture = 1;
	else
		double_gesture = 0;

	if(huawei_gesture & 0x2000)
		draw_gesture = 1;
	else
		draw_gesture = 0;

	printk("zjh: %s, wangyang g_wakeup_gesture = %d huawei_gesture = %ld double_gesture = %d draw_gesture = %d\n", __func__, ft_wakeup_gesture,huawei_gesture,double_gesture,draw_gesture);
	return size;
}
#endif
#endif
#endif


static  void tpd_down(int x, int y, int id) {
	// input_report_abs(tpd->dev, ABS_PRESSURE, p);
	input_report_key(tpd->dev, BTN_TOUCH, 1);
	input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 20);
	input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
	input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
	//printk("D[%4d %4d %4d] ", x, y, p);
	/* track id Start 0 */
#ifndef MT_PROTOCOL_B //sven  Tracking id
	input_report_abs(tpd->dev, ABS_MT_TRACKING_ID, id); 
	input_mt_sync(tpd->dev);
#endif
	TPD_EM_PRINT(x, y, x, y, id, 1);
//    if (FACTORY_BOOT == get_boot_mode()|| RECOVERY_BOOT == get_boot_mode())
//    {   
//      tpd_button(x, y, 1);  
//    }
}
 
static  void tpd_up(int x, int y) {
	//input_report_abs(tpd->dev, ABS_PRESSURE, 0);
	input_report_key(tpd->dev, BTN_TOUCH, 0);
	//input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 0);
	//input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
	//input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
	//printk("U[%4d %4d %4d] ", x, y, 0);
	input_mt_sync(tpd->dev);
	TPD_EM_PRINT(x, y, x, y, 0, 0);
//    if (FACTORY_BOOT == get_boot_mode()|| RECOVERY_BOOT == get_boot_mode())
//    {   
//       tpd_button(x, y, 0); 
//    }   		 
}

static int tpd_touchinfo(struct touch_info *cinfo, struct touch_info *pinfo,struct touch_info *ptest)
{
	int i = 0;
//#if (TPD_MAX_POINTS==2)
//	char data[35] = {0};
//#else
//	char data[16] = {0};
//#endif	
char data[128] = {0};
    u16 high_byte,low_byte,reg;
	u8 report_rate =0;

	p_point_num = point_num;
	if (tpd_halt)
	{
		TPD_DMESG( "tpd_touchinfo return ..\n");
		return false;
	}
	mutex_lock(&i2c_access);

    reg = 0x00;
	fts_i2c_Read(i2c_client, &reg, 1, data, 64);
	mutex_unlock(&i2c_access);
	//TPD_DEBUG("received raw data from touch panel as following:\n");
	//TPD_DEBUG("[data[0]=%x,data[1]= %x ,data[2]=%x ,data[3]=%x ,data[4]=%x ,data[5]=%x]\n",data[0],data[1],data[2],data[3],data[4],data[5]);
	//TPD_DEBUG("[data[9]=%x,data[10]= %x ,data[11]=%x ,data[12]=%x]\n",data[9],data[10],data[11],data[12]);
	//TPD_DEBUG("[data[15]=%x,data[16]= %x ,data[17]=%x ,data[18]=%x]\n",data[15],data[16],data[17],data[18]);

	/*get the number of the touch points*/
	point_num= data[2] & 0x0f;

	//TPD_DEBUG("point_num =%d\n",point_num);
        if(point_num>fts_updateinfo_curr.TPD_MAX_POINTS)return false;
   	if(up_flag==2)
	{
		up_flag=0;
		for(i = 0; i < fts_updateinfo_curr.TPD_MAX_POINTS; i++)     
	{
		cinfo->p[i] = data[3+6*i] >> 6; //event flag 
     	cinfo->id[i] = data[3+6*i+2]>>4; //touch id
	   	/*get the X coordinate, 2 bytes*/
		high_byte = data[3+6*i];
		high_byte <<= 8;
		high_byte &= 0x0f00;
		low_byte = data[3+6*i + 1];
		cinfo->x[i] = high_byte |low_byte;

		//cinfo->x[i] =  cinfo->x[i] * 480 >> 11; //calibra

		/*get the Y coordinate, 2 bytes*/

		high_byte = data[3+6*i+2];
		high_byte <<= 8;
		high_byte &= 0x0f00;
		low_byte = data[3+6*i+3];
		cinfo->y[i] = high_byte |low_byte;

			
			if(point_num>=i+1)
				continue;
			if(up_count==0)
				continue;
			cinfo->p[i] = ptest->p[i-point_num]; //event flag 
			
			
			cinfo->id[i] = ptest->id[i-point_num]; //touch id
	
			cinfo->x[i] = ptest->x[i-point_num];	
			
			cinfo->y[i] = ptest->y[i-point_num];
			//dev_err(&fts_i2c_client->dev," zax add two x = %d, y = %d, evt = %d,id=%d\n", cinfo->x[i], cinfo->y[i], cinfo->p[i], cinfo->id[i]);
			up_count--;
			
				
		}
		if(cinfo->p[i] == 0)//down
			printk("[FTS]tpd Down P[%d]:x=%d,y= %d\n",cinfo->id[i],cinfo->x[i],cinfo->y[i]);
		else if(cinfo->p[i] == 1)//up
			printk("[FTS]tpd Up P[%d]:x=%d,y= %d\n",cinfo->id[i],cinfo->x[i],cinfo->y[i]);
		
	//TPD_DEBUG(" cinfo->x[0] = %d, cinfo->y[0] = %d, cinfo->p[0] = %d\n", cinfo->x[0], cinfo->y[0], cinfo->p[0]);	
	//TPD_DEBUG(" cinfo->x[1] = %d, cinfo->y[1] = %d, cinfo->p[1] = %d\n", cinfo->x[1], cinfo->y[1], cinfo->p[1]);		
	//TPD_DEBUG(" cinfo->x[2]= %d, cinfo->y[2]= %d, cinfo->p[2] = %d\n", cinfo->x[2], cinfo->y[2], cinfo->p[2]);	

	return true;
	}
	up_count=0;
	for(i = 0; i < fts_updateinfo_curr.TPD_MAX_POINTS; i++)  
	{
		cinfo->p[i] = data[3+6*i] >> 6; //event flag 
		
		if(0==cinfo->p[i])
		{
			//dev_err(&fts_i2c_client->dev,"\n	zax enter add	\n");
			up_flag=1;
		}
			cinfo->id[i] = data[3+6*i+2]>>4; //touch id
		/*get the X coordinate, 2 bytes*/
		high_byte = data[3+6*i];
		high_byte <<= 8;
		high_byte &= 0x0f00;
		low_byte = data[3+6*i + 1];
		cinfo->x[i] = high_byte |low_byte;	
		high_byte = data[3+6*i+2];
		high_byte <<= 8;
		high_byte &= 0x0f00;
		low_byte = data[3+6*i+3];
		cinfo->y[i] = high_byte |low_byte;

		if(up_flag==1 && 1==cinfo->p[i])
		{
			up_flag=2;
			point_num++;
			ptest->x[up_count]=cinfo->x[i];
			ptest->y[up_count]=cinfo->y[i];
			ptest->id[up_count]=cinfo->id[i];
			ptest->p[up_count]=cinfo->p[i];
			//dev_err(&fts_i2c_client->dev," zax add x = %d, y = %d, evt = %d,id=%d\n", ptest->x[j], ptest->y[j], ptest->p[j], ptest->id[j]);
			cinfo->p[i]=2;
			up_count++;
		}
	}
	if(up_flag==1)
		up_flag=0;
	//printk(" tpd cinfo->x[0] = %d, cinfo->y[0] = %d, cinfo->p[0] = %d\n", cinfo->x[0], cinfo->y[0], cinfo->p[0]);
	return true;

}

#ifdef TPD_PROXIMITY
static int tpd_get_ps_value(void)
{
	return tpd_proximity_detect;
}

static int tpd_enable_ps(int enable)
{
	u8 state, state2;
	int ret = -1;

	i2c_smbus_read_i2c_block_data(i2c_client, 0xB0, 1, &state);
	printk("[proxi_5206]read: 999 0xb0's value is 0x%02X\n", state);
	if (enable){
		state |= 0x01;
		tpd_proximity_flag = 1;
		TPD_PROXIMITY_DEBUG("[proxi_5206]ps function is on\n");	
	}else{
		state &= 0x00;	
		tpd_proximity_flag = 0;
		TPD_PROXIMITY_DEBUG("[proxi_5206]ps function is off\n");
	}

	ret = i2c_smbus_write_i2c_block_data(i2c_client, 0xB0, 1, &state);
	TPD_PROXIMITY_DEBUG("[proxi_5206]write: 0xB0's value is 0x%02X\n", state);

	i2c_smbus_read_i2c_block_data(i2c_client, 0xB0, 1, &state2);
	if(state!=state2)
	{
		tpd_proximity_flag=0;
		printk("[proxi_5206]ps fail!!! state = 0x%x,  state2 =  0x%X\n", state,state2);
	}

	return 0;
}

static int tpd_ps_operate(void* self, uint32_t command, void* buff_in, int size_in,
		void* buff_out, int size_out, int* actualout)
{
	int err = 0;
	int value;

	hwm_sensor_data *sensor_data;
	TPD_DEBUG("[proxi_5206]command = 0x%02X\n", command);		
	switch (command)
	{
		case SENSOR_DELAY:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				APS_ERR("Set delay parameter error!\n");
				err = -EINVAL;
			}
			// Do nothing
			break;

		case SENSOR_ENABLE:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				APS_ERR("Enable sensor parameter error!\n");
				err = -EINVAL;
			}
			else
			{
				value = *(int *)buff_in;
				if(value)
				{
					if((tpd_enable_ps(1) != 0))
					{
						APS_ERR("enable ps fail: %d\n", err); 
						return -1;
					}
				}
				else
				{
					if((tpd_enable_ps(0) != 0))
					{
						APS_ERR("disable ps fail: %d\n", err);
						return -1;
					}
				}
			}
			break;

		case SENSOR_GET_DATA:
			if((buff_out == NULL) || (size_out< sizeof(hwm_sensor_data)))
			{
				APS_ERR("get sensor data parameter error!\n");
				err = -EINVAL;
			}
			else
			{

				sensor_data = (hwm_sensor_data *)buff_out;

				sensor_data->values[0] = tpd_get_ps_value();
				TPD_PROXIMITY_DEBUG("huang sensor_data->values[0] 1082 = %d\n", sensor_data->values[0]);
				sensor_data->value_divide = 1;
				sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
			}
			break;
		default:
			APS_ERR("proxmy sensor operate function no this parameter %d!\n", command);
			err = -1;
			break;
	}

	return err;
}
#endif

#ifdef MT_PROTOCOL_B
/************************************************************************
* Name: fts_read_Touchdata
* Brief: report the point information
* Input: event info
* Output: get touch data in pinfo
* Return: success is zero
***********************************************************************/
static unsigned int buf_count_add=0;
static unsigned int buf_count_neg=0;
u8 buf_touch_data[30*POINT_READ_BUF] = { 0 };//0xFF
static int fts_read_Touchdata(struct ts_event *pinfo)
{
       u8 buf[POINT_READ_BUF] = { 0 };
	int ret = -1;
	int i = 0;
	u8 pointid = FTS_MAX_ID;

	if (tpd_halt)
	{
		TPD_DMESG( "tpd_touchinfo return ..\n");
		return false;
	}

	mutex_lock(&i2c_access);
	ret = fts_i2c_Read(i2c_client, buf, 1, buf, POINT_READ_BUF);
	if (ret < 0)
	{
		dev_err(&i2c_client->dev, "%s read touchdata failed.\n",__func__);
		mutex_unlock(&i2c_access);
		return ret;
	}
	mutex_unlock(&i2c_access);
	buf_count_add++;
	memcpy( buf_touch_data+(((buf_count_add-1)%30)*POINT_READ_BUF), buf, sizeof(u8)*POINT_READ_BUF );
	return 0;
}
static int fts_report_value(struct ts_event *data)
 {
	int i = 0,j = 0;
	int up_point = 0;
 	int touchs = 0;
	u8 pointid = FTS_MAX_ID;
	u8 buf[POINT_READ_BUF] = { 0 };//0xFF
	buf_count_neg++;
	memcpy( buf,buf_touch_data+(((buf_count_neg-1)%30)*POINT_READ_BUF), sizeof(u8)*POINT_READ_BUF );
	memset(data, 0, sizeof(struct ts_event));
	data->touch_point_num=buf[FT_TOUCH_POINT_NUM] & 0x0F;
	
	data->touch_point = 0;
	//printk("tpd  fts_updateinfo_curr.TPD_MAX_POINTS=%d fts_updateinfo_curr.chihID=%d \n", fts_updateinfo_curr.TPD_MAX_POINTS,fts_updateinfo_curr.CHIP_ID);
	for (i = 0; i < fts_updateinfo_curr.TPD_MAX_POINTS; i++)
	{
		pointid = (buf[FTS_TOUCH_ID_POS + FTS_TOUCH_STEP * i]) >> 4;
		if (pointid >= FTS_MAX_ID)
			break;
		else
			data->touch_point++;
		data->au16_x[i] =
		    (s16) (buf[FTS_TOUCH_X_H_POS + FTS_TOUCH_STEP * i] & 0x0F) <<
		    8 | (s16) buf[FTS_TOUCH_X_L_POS + FTS_TOUCH_STEP * i];
		data->au16_y[i] =
		    (s16) (buf[FTS_TOUCH_Y_H_POS + FTS_TOUCH_STEP * i] & 0x0F) <<
		    8 | (s16) buf[FTS_TOUCH_Y_L_POS + FTS_TOUCH_STEP * i];
		data->au8_touch_event[i] =
		    buf[FTS_TOUCH_EVENT_POS + FTS_TOUCH_STEP * i] >> 6;
		data->au8_finger_id[i] =
		    (buf[FTS_TOUCH_ID_POS + FTS_TOUCH_STEP * i]) >> 4;
		data->pressure[i] =
			(buf[FTS_TOUCH_XY_POS + FTS_TOUCH_STEP * i]);//cannot constant value
		data->area[i] =
			(buf[FTS_TOUCH_MISC + FTS_TOUCH_STEP * i]) >> 4;
		if((data->au8_touch_event[i]==0 || data->au8_touch_event[i]==2)&&((data->touch_point_num==0)||(data->pressure[i]==0 && data->area[i]==0  )))
			return 1;



		//if ( pinfo->au16_x[i]==0 && pinfo->au16_y[i] ==0)
		//	pt00f++;
	}
	/*
	if (pt00f>0)
	{
		for(i=0;i<POINT_READ_BUF;i++)
		{
        		printk(KERN_WARNING "The xy00 is %x \n",buf[i]);
		}
		printk(KERN_WARNING "\n");
	}
	*/
	//event = data;
	for (i = 0; i < data->touch_point; i++)
	{
		 input_mt_slot(tpd->dev, data->au8_finger_id[i]);

		if (data->au8_touch_event[i]== 0 || data->au8_touch_event[i] == 2)
		{
			 input_mt_report_slot_state(tpd->dev, MT_TOOL_FINGER,true);
			 input_report_abs(tpd->dev, ABS_MT_PRESSURE,data->pressure[i]/*0x3f*/);
			 input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR,data->area[i]/*0x05*/);
			 input_report_abs(tpd->dev, ABS_MT_POSITION_X,data->au16_x[i]);
			 input_report_abs(tpd->dev, ABS_MT_POSITION_Y,data->au16_y[i]);
			 touchs |= BIT(data->au8_finger_id[i]);
   			 data->touchs |= BIT(data->au8_finger_id[i]);
             		 //printk("tpd D x[%d] =%d,y[%d]= %d",i,data->au16_x[i],i,data->au16_y[i]);
		}
		else
		{
			 up_point++;
			 input_mt_report_slot_state(tpd->dev, MT_TOOL_FINGER,false);
			 data->touchs &= ~BIT(data->au8_finger_id[i]);
		}

	}
 	if(unlikely(data->touchs ^ touchs)){
		for(i = 0; i < CFG_MAX_TOUCH_POINTS; i++){
			if(BIT(i) & (data->touchs ^ touchs)){
				input_mt_slot(tpd->dev, i);
				input_mt_report_slot_state(tpd->dev, MT_TOOL_FINGER, false);
			}
		}
	}
	data->touchs = touchs;
	//wangyang add begin 2015 12 30
	if (data->touch_point_num == 0)
	{
		// release all touches
		for(j = 0; j < CFG_MAX_TOUCH_POINTS; j++)
		{
			input_mt_slot( tpd->dev, j);
			input_mt_report_slot_state( tpd->dev, MT_TOOL_FINGER, false);
		}
		data->touchs = 0;
		input_report_key(tpd->dev, BTN_TOUCH, 0);
		input_sync(tpd->dev);
		return;
	}
	//wangyang add end 2015 12 30
	if(data->touch_point == up_point)
		 input_report_key(tpd->dev, BTN_TOUCH, 0);
	else
		 input_report_key(tpd->dev, BTN_TOUCH, 1);

	input_sync(tpd->dev);
	return 0;
    	//printk("tpd D x =%d,y= %d",event->au16_x[0],event->au16_y[0]);
 }

#endif


#ifdef FTS_GESTRUE
static void check_gesture(int gesture_id)
{
	
    printk("kaka gesture_id==0x%x\n ",gesture_id);
#if defined(FTS_HUAWEI_DESIGN)
	switch(gesture_id)
	{

		case GESTURE_DOUBLECLICK:
			if(double_gesture){
				input_report_key(tpd->dev, KEY_F1, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F1, 0);
				input_sync(tpd->dev);
				}
			break;
		case GESTURE_C:
			if(draw_gesture){
				input_report_key(tpd->dev, KEY_F8, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F8, 0);
				input_sync(tpd->dev);
				}
			break;
		case GESTURE_E:
			if(draw_gesture){
				input_report_key(tpd->dev, KEY_F9, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F9, 0);
				input_sync(tpd->dev);
				}
			break;

		case GESTURE_M:
				if(draw_gesture){
				input_report_key(tpd->dev, KEY_F10, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F10, 0);
				input_sync(tpd->dev);
				}
			break;
			
		case GESTURE_W:
				if(draw_gesture){
				input_report_key(tpd->dev, KEY_F11, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F11, 0);
				input_sync(tpd->dev);
				}
			break;
		default:
			break;
	}
#else 
	switch(gesture_id)
	{

		case GESTURE_DOUBLECLICK:
				input_report_key(tpd->dev, KEY_F1, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F1, 0);
				input_sync(tpd->dev);
			break;
		case GESTURE_C:
				input_report_key(tpd->dev, KEY_F8, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F8, 0);
				input_sync(tpd->dev);
			break;

			
		case GESTURE_E:
				input_report_key(tpd->dev, KEY_F9, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F9, 0);
				input_sync(tpd->dev);
			break;

		case GESTURE_M:
				input_report_key(tpd->dev, KEY_F10, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F10, 0);
				input_sync(tpd->dev);
			break;
			
		case GESTURE_W:
				input_report_key(tpd->dev, KEY_F11, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_F11, 0);
				input_sync(tpd->dev);
			break;
		default:
			break;
	}
#endif
/*
		case GESTURE_W:
				input_report_key(tpd->dev, KEY_GESTURE_W, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_GESTURE_W, 0);
				input_sync(tpd->dev);
			break;

		case GESTURE_M:
				input_report_key(tpd->dev, KEY_GESTURE_M, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_GESTURE_V, 0);
				input_sync(tpd->dev);
			break;

		case GESTURE_S:
				input_report_key(tpd->dev, KEY_GESTURE_S, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_GESTURE_S, 0);
				input_sync(tpd->dev);
			break;

		case GESTURE_Z:
				input_report_key(tpd->dev, KEY_GESTURE_Z, 1);
				input_sync(tpd->dev);
				input_report_key(tpd->dev, KEY_GESTURE_Z, 0);
				input_sync(tpd->dev);
			break;
*/

}

static int ft5x0x_read_Touchdata(void)
{
		unsigned char buf[FTS_GESTRUE_POINTS * 4+2+36] = { 0 };
		//unsigned char buf[FTS_GESTRUE_POINTS * 2] = { 0 }; 
		int ret = -1;
		int i = 0;
		buf[0] = 0xd3;
		int gestrue_id = 0;
    short pointnum = 0;
		pointnum = 0;
		short feature_codes = 0x1111;
		u32 gesture_start_point_x,gesture_start_point_y;
		u32 gesture_end_point_x,gesture_end_point_y;
    		u32 gesture_p1_point_x,gesture_p1_point_y;
    		u32 gesture_p2_point_x,gesture_p2_point_y;
    		u32 gesture_p3_point_x,gesture_p3_point_y;
    		u32 gesture_p4_point_x,gesture_p4_point_y;
		mutex_lock(&i2c_access);	
		ret = fts_i2c_Read(i2c_client, buf, 1, buf, FTS_GESTRUE_POINTS_HEADER);
		mutex_unlock(&i2c_access);
		//ret = i2c_smbus_read_i2c_block_data(i2c_client, 0xd3, FTS_GESTRUE_POINTS_HEADER, buf);
		//i2c_smbus_write_i2c_block_data(i2c_client, 0xd3, 0, &buf);
		//i2c_smbus_read_i2c_block_data(i2c_client, 0xd3, FTS_GESTRUE_POINTS_HEADER, &buf);
		if (ret < 0)
		{
			printk( "%s read touchdata failed.\n", __func__);
			mutex_unlock(&i2c_access);
			return ret;
		}
		mutex_unlock(&i2c_access);
		//printk("ft5x0x_read_Touchdata buf[0]=%x \n",buf[0]);
		/* FW */

  		if(buf[0]!=0xfe)
    		{
      			  gestrue_id =  buf[0];
      			  //check_gesture(gestrue_id);
		}
		mutex_lock(&i2c_access);
    		pointnum = (short)(buf[1]) & 0xff;
    		buf[0] = 0xd3;
    		if((pointnum * 4 + 2 +36)<255) //  + 6
    		{
    			ret = fts_i2c_Read(i2c_client, buf, 1, buf, (pointnum * 4 + 2 + 36)); //  + 6
   		 }
    		else
   		 {
    			 ret = fts_i2c_Read(i2c_client, buf, 1, buf, 255);
         		 ret = fts_i2c_Read(i2c_client, buf, 0, buf+255, (pointnum * 4 + 2 + 36)-255); // + 6

    		  }
	        mutex_unlock(&i2c_access);		
   		if (ret < 0)
    		{
        		printk( "%s read touchdata failed.\n", __func__);
			mutex_unlock(&i2c_access);
        		return ret;
    		}
		mutex_unlock(&i2c_access);
		for(i=0;i<18;i++)
		{
			feature_codes = (buf[pointnum * 4 + 2+i]<<8)|buf[pointnum * 4 + 2+1+i]; //  + 6
			printk("0x%X ", feature_codes);
		}  
			printk("0x%X  wangyang the pointnum = %d ", feature_codes,pointnum);
        		gesture_start_point_x   = (buf[pointnum * 4 + 2+4]<<8)|buf[pointnum * 4 + 2+1+4];
        		gesture_start_point_y  = (buf[pointnum * 4 + 2+6] << 8)|buf[pointnum * 4 + 2+1+6];
        		printk("gesture_start_point_x =0x%X, gesture_start_point_y =0x%X\n", gesture_start_point_x,gesture_start_point_y);
        		gesture_end_point_x   = (buf[pointnum * 4 + 2+8] << 8)|buf[pointnum * 4 + 2+1+8];
        		gesture_end_point_y  = (buf[pointnum * 4 + 2+10] << 8)|buf[pointnum * 4 + 2+1+10];
        		printk("gesture_end_point_x = 0x%X, gesture_end_point_y = 0x%X\n", gesture_end_point_x,gesture_end_point_y);
        		gesture_p1_point_x   = (buf[pointnum * 4 + 2+20] << 8)|buf[pointnum * 4 + 2+1+20];
        		gesture_p1_point_y   = (buf[pointnum * 4 + 2+22] << 8)|buf[pointnum * 4 + 2+1+22];
        		printk("gesture_p1_point_x = 0x%X, gesture_p1_point_y = 0x%X\n", gesture_p1_point_x,gesture_p1_point_y);
        		gesture_p2_point_x   = (buf[pointnum * 4 + 2+24] << 8)|buf[pointnum * 4 + 2+1+24];
        		gesture_p2_point_y   = (buf[pointnum * 4 + 2+26] << 8)|buf[pointnum * 4 + 2+1+26];
        		printk("gesture_p2_point_x = 0x%X, gesture_p2_point_y = 0x%X\n", gesture_p2_point_x,gesture_p2_point_y);
        		gesture_p3_point_x   = (buf[pointnum * 4 + 2+28] << 8)|buf[pointnum * 4 + 2+1+28];
        		gesture_p3_point_y   = (buf[pointnum * 4 + 2+30] << 8)|buf[pointnum * 4 + 2+1+30];
        		printk("gesture_p3_point_x = 0x%X, gesture_p3_point_y = 0x%X\n", gesture_p3_point_x,gesture_p3_point_y);
        		gesture_p4_point_x   = (buf[pointnum * 4 + 2+32] << 8)|buf[pointnum * 4 + 2+1+32];
        		gesture_p4_point_y   = (buf[pointnum * 4 + 2+34] << 8)|buf[pointnum * 4 + 2+1+34];
        		printk("gesture_p4_point_x = 0x%X, gesture_p4_point_y = 0x%X\n", gesture_p4_point_x,gesture_p4_point_y);
				gesture_echo[0]  = gesture_start_point_x;
				gesture_echo[1]  = gesture_start_point_y;	
				gesture_echo[2]  = gesture_end_point_x;
				gesture_echo[3]  = gesture_end_point_y;		
				gesture_echo[4]  = gesture_p1_point_x;
				gesture_echo[5] = gesture_p1_point_y;	
				gesture_echo[6] = gesture_p2_point_x;
				gesture_echo[7] = gesture_p2_point_y;	
				gesture_echo[8] = gesture_p3_point_x;
				gesture_echo[9] = gesture_p3_point_y;	
				gesture_echo[10] = gesture_p4_point_x;
				gesture_echo[11] = gesture_p4_point_y;	
				check_gesture(gestrue_id);
//add end
        	 return -1;
 }
	

#endif
 static int touch_event_handler(void *unused)
{ 
	struct touch_info cinfo, pinfo, ptest;
#ifdef MT_PROTOCOL_B
	struct ts_event pevent;
	int ret = 0;
#endif
	int i=0;

	struct sched_param param = { .sched_priority = RTPM_PRIO_TPD };
	sched_setscheduler(current, SCHED_RR, &param);

#ifdef TPD_PROXIMITY
	int err;
	hwm_sensor_data sensor_data;
	u8 proximity_status;
	
#endif
	u8 state;

	do
	{
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
		set_current_state(TASK_INTERRUPTIBLE); 
	//	printk("wangcq327 --- waitting\n");
		wait_event_interruptible(waiter,tpd_flag!=0);				 
//		printk("wangcq327 --- pass\n");
		tpd_flag = 0;

		set_current_state(TASK_RUNNING);
#ifdef FTS_GESTRUE
		if(tpd_halt == 2)	continue;
		mutex_lock(&i2c_access);
		fts_read_reg(i2c_client,0xd0,&state);
		mutex_unlock(&i2c_access);
	    printk("[FTS]%s,Reg0xd0 = %d\n",__func__,state);
//		if((get_suspend_state() == PM_SUSPEND_MEM) && (state ==1))
		if(state ==1)
		{
			ft5x0x_read_Touchdata();
			continue;
		}
#endif

#ifdef TPD_PROXIMITY
		if (tpd_proximity_flag == 1)
		{
			i2c_smbus_read_i2c_block_data(i2c_client, 0xB0, 1, &state);
			TPD_PROXIMITY_DEBUG("proxi_5206 0xB0 state value is 1131 0x%02X\n", state);

			if(!(state&0x01))
			{
				tpd_enable_ps(1);
			}

			i2c_smbus_read_i2c_block_data(i2c_client, 0x01, 1, &proximity_status);
			TPD_PROXIMITY_DEBUG("proxi_5206 0x01 value is 1139 0x%02X\n", proximity_status);

			if (proximity_status == 0xC0)
			{
				tpd_proximity_detect = 0;	
			}
			else if(proximity_status == 0xE0)
			{
				tpd_proximity_detect = 1;
			}

			TPD_PROXIMITY_DEBUG("tpd_proximity_detect 1149 = %d\n", tpd_proximity_detect);

			if(tpd_proximity_detect != tpd_proximity_detect_prev)
			{
				tpd_proximity_detect_prev = tpd_proximity_detect;
				sensor_data.values[0] = tpd_get_ps_value();
				sensor_data.value_divide = 1;
				sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
				if ((err = hwmsen_get_interrupt_data(ID_PROXIMITY, &sensor_data)))
				{
					TPD_PROXIMITY_DMESG(" proxi_5206 call hwmsen_get_interrupt_data failed= %d\n", err);	
				}
			}
		}  
#endif
#ifdef MT_PROTOCOL_B
		{
        	ret = fts_read_Touchdata(&pevent);
			if (ret == 0)
				fts_report_value(&pevent);
		}
#else

		if (tpd_touchinfo(&cinfo, &pinfo,&ptest)) 
		{
			TPD_DEBUG("point_num = %d\n",point_num);
			TPD_DEBUG_SET_TIME;
			if(point_num >0) 
			{
				for(i = 0; i < fts_updateinfo_curr.TPD_MAX_POINTS; i++)  
				{
					if((0==cinfo.p[i]) || (2==cinfo.p[i]))
					{
			       			tpd_down(cinfo.x[i], cinfo.y[i], cinfo.id[i]);
					}
					
				}
				input_sync(tpd->dev);
			}
			else
			{
				tpd_up(cinfo.x[0], cinfo.y[0]);
				//TPD_DEBUG("release --->\n"); 
				//input_mt_sync(tpd->dev);
				input_sync(tpd->dev);
			}
		}
#endif
		if(tpd_mode==12)
		{
			//power down for desence debug
			//power off, need confirm with SA
#ifdef TPD_POWER_SOURCE_CUSTOM
#ifdef CONFIG_ARCH_MT6580
			ret = regulator_disable(tpd->reg); //disable regulator
			if(ret)
			{
				printk("focaltech __touch_event_handler()__ regulator_disable() failed!\n");
			}
#else

			hwPowerDown(TPD_POWER_SOURCE_CUSTOM, "TP");
#endif
#else
			hwPowerDown(MT65XX_POWER_LDO_VGP2, "TP");
#endif

#ifdef TPD_POWER_SOURCE_1800
			hwPowerDown(TPD_POWER_SOURCE_1800, "TP");
#endif
			msleep(20);
		}
	}while(!kthread_should_stop());

	return 0;
}
 
static int tpd_detect (struct i2c_client *client, struct i2c_board_info *info) 
{
	strcpy(info->type, TPD_DEVICE);	
	return 0;
}
 
static void tpd_eint_interrupt_handler(void)
{
	//TPD_DEBUG("TPD interrupt has been triggered\n");
	TPD_DEBUG_PRINT_INT;
	tpd_flag = 1;
	wake_up_interruptible(&waiter);
}

void focaltech_get_upgrade_array(void)
{
	u8 chip_id;
	u32 i;

	i2c_smbus_read_i2c_block_data(i2c_client,FT_REG_CHIP_ID,1,&chip_id);

	DBG("%s chip_id = %x\n", __func__, chip_id);
	for(i=0;i<sizeof(fts_updateinfo)/sizeof(struct Upgrade_Info);i++)
	{
		if(chip_id==fts_updateinfo[i].CHIP_ID)
		{
			memcpy(&fts_updateinfo_curr, &fts_updateinfo[i], sizeof(struct Upgrade_Info));
			break;
		}
	}

	if(i >= sizeof(fts_updateinfo)/sizeof(struct Upgrade_Info))
	{
		memcpy(&fts_updateinfo_curr, &fts_updateinfo[0], sizeof(struct Upgrade_Info));
	}
}


static int /*__devinit*/ tpd_probe(struct i2c_client *client, const struct i2c_device_id *id)
{	 
	int retval = TPD_OK;
	char data;
	u8 report_rate=0;
	int err=0;
	int reset_count = 0;
	u8 chip_id,i;
#ifdef CONFIG_ARCH_MT6580
	int ret = 0;
#endif
	u8 uc_tp_fm_ver;
	client->timing = 200;

	i2c_client = client;                                        //julian 5.16
	//power on, need confirm with SA
	mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);  
	msleep(5);
	DBG(" fts ic reset\n");
//	DBG("wangcq327 --- %d\n",TPD_POWER_SOURCE_CUSTOM);	
#ifdef TPD_POWER_SOURCE_CUSTOM
#ifdef CONFIG_ARCH_MT6580
	tpd->reg = regulator_get(tpd->tpd_dev,TPD_POWER_SOURCE_CUSTOM); // get pointer to regulator structure
	if (IS_ERR(tpd->reg))
	{
		printk("focaltech tpd_probe regulator_get() failed!!!\n");
	}

	ret = regulator_set_voltage(tpd->reg, 2800000, 2800000);	// set 2.8v
	if(ret)
	{
		printk("focaltech tpd_probe regulator_set_voltage() failed!\n");
	}
	ret = regulator_enable(tpd->reg);  //enable regulator
	if (ret)
	{
		printk("focaltech tpd_probe regulator_enable() failed!\n");
	}
#else
	hwPowerOn(TPD_POWER_SOURCE_CUSTOM, VOL_2800, "TP");
#endif
#else
	hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_2800, "TP");
#endif

#if 0 //def TPD_POWER_SOURCE_1800
	hwPowerOn(TPD_POWER_SOURCE_1800, VOL_1800, "TP");
#endif
	msleep(5);
	mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
	msleep(200);

 //if((i2c_smbus_read_i2c_block_data(i2c_client, 0x00, 1, &data))< 0)
 ///////////////////////////////////////////////
 reset_proc:
	err = i2c_smbus_read_i2c_block_data(i2c_client, 0x00, 1, &data);

	DBG("gao_i2c:err %d,data:%d\n", err,data);
	if(err< 0 || data!=0)// reg0 data running state is 0; other state is not 0
	{
		DBG("I2C transfer error, line: %d\n", __LINE__);
#ifdef TPD_RESET_ISSUE_WORKAROUND
        if ( ++reset_count < TPD_MAX_RESET_COUNT )
        {
            		
#ifdef TPD_CLOSE_POWER_IN_SLEEP	 
	hwPowerDown(TPD_POWER_SOURCE,"TP");
	hwPowerOn(TPD_POWER_SOURCE,VOL_3300,"TP");
	msleep(100);
#else
		mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
		mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);  
		msleep(5);
		DBG(" fts ic reset\n");
		mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
		mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
		msleep(200);
#endif
		goto reset_proc;	
            
        }
#endif
#ifdef TPD_POWER_SOURCE_CUSTOM
#ifdef CONFIG_ARCH_MT6580
	ret	= regulator_disable(tpd->reg); //disable regulator
	if(ret)
	{
		printk("focaltech tpd_probe regulator_disable() failed!\n");
	}

	regulator_put(tpd->reg);
#else
		hwPowerDown(TPD_POWER_SOURCE_CUSTOM, "TP");
#endif
#else
		hwPowerDown(MT65XX_POWER_LDO_VGP2, "TP");
#endif
		return -1; 
	}

	//msleep(200);
	
	mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
	mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
	mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(GPIO_CTP_EINT_PIN, GPIO_PULL_UP);
 
	//mt_eint_set_sens(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_SENSITIVE);
	//mt_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_DEBOUNCE_CN);
	//mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_TYPE, tpd_eint_interrupt_handler, 1); 
	mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, EINTF_TRIGGER_FALLING, tpd_eint_interrupt_handler, 1); 


	tpd_load_status = 1;
 //   tpd_mstar_status =0 ;  // compatible mstar and ft6306 chenzhecong
 
	focaltech_get_upgrade_array();

#ifdef TPD_SYSFS_DEBUG
	fts_create_sysfs(i2c_client);
#ifdef FTS_APK_DEBUG
    		fts_create_apk_debug_channel(i2c_client);
    #endif
#endif
#ifdef FTS_CTL_IIC
	if (ft_rw_iic_drv_init(i2c_client) < 0)
		DBG("%s:[FTS] create fts control iic driver failed\n",__func__);
#endif

#ifdef  TP_MONITOR_CTL                                 //                       //
       fts_write_reg(i2c_client, 0x86, 0x00);			
#endif        
/*
#ifdef FTS_HUAWEI_DESIGN
	ft5436_echo_proc = proc_create(GT9XX_ECHO_PROC_FILE, 0666, NULL, &configecho_proc_ops);
	if (ft5436_echo_proc == NULL)
	{
		printk("create_proc_entry %s failed\n", GT9XX_ECHO_PROC_FILE);
	}
#endif
*/
#ifdef VELOCITY_CUSTOM_FT5206
	if((err = misc_register(&tpd_misc_device)))
	{
		printk("mtk_tpd: tpd_misc_device register failed\n");
	}
#endif

#ifdef TPD_AUTO_UPGRADE
	printk("********************Enter CTP Auto Upgrade********************\n");
	fts_ctpm_auto_upgrade(i2c_client);
#endif
	fts_read_reg(i2c_client, FTS_REG_FW_VER, &uc_tp_fm_ver);

	tpd->dev->id.version=(uc_tp_fm_ver&0xff);
	printk("[FTS] tpd->dev->id.version = 0x%x\n",tpd->dev->id.version);
	hardware_info.tpd_fw_version = tpd->dev->id.version;
	thread = kthread_run(touch_event_handler, 0, TPD_DEVICE);
	if (IS_ERR(thread))
	{
		retval = PTR_ERR(thread);
		DBG(" failed to create kernel thread: %d\n", retval);
	}
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);

	DBG("FTS Touch Panel Device Probe %s\n", (retval < TPD_OK) ? "FAIL" : "PASS");

#ifdef FTS_GESTRUE
	
    input_set_capability(tpd->dev, EV_KEY, KEY_POWER);
    input_set_capability(tpd->dev, EV_KEY, KEY_F1);
    input_set_capability(tpd->dev, EV_KEY, KEY_F2);
    input_set_capability(tpd->dev, EV_KEY, KEY_F3);
    input_set_capability(tpd->dev, EV_KEY, KEY_F4);
    input_set_capability(tpd->dev, EV_KEY, KEY_F5);
    input_set_capability(tpd->dev, EV_KEY, KEY_F6);
    input_set_capability(tpd->dev, EV_KEY, KEY_F7);
    input_set_capability(tpd->dev, EV_KEY, KEY_F8);
    input_set_capability(tpd->dev, EV_KEY, KEY_F9);
    input_set_capability(tpd->dev, EV_KEY, KEY_F10);
    input_set_capability(tpd->dev, EV_KEY, KEY_F11);
    input_set_capability(tpd->dev, EV_KEY, KEY_F12);

	__set_bit(KEY_F1, tpd->dev->keybit);
	__set_bit(KEY_F2, tpd->dev->keybit);
	__set_bit(KEY_F3, tpd->dev->keybit);
	__set_bit(KEY_F4, tpd->dev->keybit);
	__set_bit(KEY_F5, tpd->dev->keybit);
	__set_bit(KEY_F6, tpd->dev->keybit);
	__set_bit(KEY_F7, tpd->dev->keybit);
	__set_bit(KEY_F8, tpd->dev->keybit);
	__set_bit(KEY_F9, tpd->dev->keybit);
	__set_bit(KEY_F10, tpd->dev->keybit);
	__set_bit(KEY_F11, tpd->dev->keybit);
	__set_bit(KEY_F12, tpd->dev->keybit);

	__set_bit(EV_KEY, tpd->dev->evbit);
	__set_bit(EV_SYN, tpd->dev->evbit);
#endif
#ifdef TPD_PROXIMITY
	struct hwmsen_object obj_ps;

	obj_ps.polling = 0;//interrupt mode
	obj_ps.sensor_operate = tpd_ps_operate;
	if((err = hwmsen_attach(ID_PROXIMITY, &obj_ps)))
	{
		DBG("proxi_fts attach fail = %d\n", err);
	}
	else
	{
		DBG("proxi_fts attach ok = %d\n", err);
	}		
#endif

#ifdef MT_PROTOCOL_B
	#if 1//(LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
		input_mt_init_slots(tpd->dev, MT_MAX_TOUCH_POINTS,1);
	#endif
		input_set_abs_params(tpd->dev, ABS_MT_TOUCH_MAJOR,0, 255, 0, 0);
		input_set_abs_params(tpd->dev, ABS_MT_POSITION_X, 0, TPD_RES_X, 0, 0);
		input_set_abs_params(tpd->dev, ABS_MT_POSITION_Y, 0, TPD_RES_Y, 0, 0);
		input_set_abs_params(tpd->dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
#endif
#ifdef USB_CHARGE_DETECT
	ctp_is_probe = 1;
#endif

#ifdef HQ_DBG_CTP_INFO
static struct hq_dbg_entry ctp_proc_entry[]=
   {
   		{
			.name="firmware_id",
			{	.owner = THIS_MODULE,
				.read=proc_get_ctp_firmware_id,
				.write=NULL,
			} 
		},
#ifdef FTS_GESTRUE
		{
		   .name="gesture_switch",	
	        {
			    .owner = THIS_MODULE,
			    .read  = gesture_switch_read_proc,
			    .write = gesture_switch_write_proc,
		  	}
		},
#if defined(FTS_HUAWEI_DESIGN)
	{
	   .name="g_huawei_control",	
          {
	    .owner = THIS_MODULE,
	    .read  = huawei_gesture_switch_read_proc,
	    .write = huawei_gesture_switch_write_proc,
	  }
	},
	{
	   .name="gesture_echo",	
          {
	    .owner = THIS_MODULE,
	    .read  = ft5436echo_read_proc,
	    .write = ft5436echo_write_proc,
	  }
	},
#endif
#endif
#ifdef HQ_GTP_GLOVE
		{
			.name = "glove_switch",
			{
				.owner = THIS_MODULE,
				.read  = glove_switch_read_proc,
				.write = glove_switch_write_proc,
			}
		},
#endif
	};
	//hq_gt9xx_client=client;
	proc_hq_dbg_add("ctp", ctp_proc_entry, sizeof(ctp_proc_entry)/sizeof(struct hq_dbg_entry));
#endif	

	return 0;
 }

 static int tpd_remove(struct i2c_client *client)
{

#ifdef FTS_APK_DEBUG
	fts_release_apk_debug_channel();
#endif
#ifdef TPD_SYSFS_DEBUG
	fts_release_sysfs(client);
#endif

#ifdef FTS_CTL_IIC
	ft_rw_iic_drv_exit();
#endif

#ifdef TPD_POWER_SOURCE_CUSTOM
#ifdef CONFIG_ARCH_MT6580
	regulator_disable(tpd->reg); //disable regulator
	regulator_put(tpd->reg);
#endif
#endif

	TPD_DEBUG("TPD removed\n");

   	return 0;
}
 
static int tpd_local_init(void)
{
  	DBG("FTS I2C Touchscreen Driver (Built %s @ %s)\n", __DATE__, __TIME__);
 
   	if(i2c_add_driver(&tpd_i2c_driver)!=0)
   	{
  		DBG("FTS unable to add i2c driver.\n");
      	return -1;
    }
    if(tpd_load_status == 0) 
    {
    	DBG("FTS add error touch panel driver.\n");
    	i2c_del_driver(&tpd_i2c_driver);
    	return -1;
    }

#ifdef TPD_HAVE_BUTTON     
	//if(TPD_RES_Y > 854)
	{
	    tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);// initialize tpd button data
	}
	//else
	{
	    //tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local_fwvga);// initialize tpd button data
	}
#endif   
#ifndef MT_PROTOCOL_B //sven  Tracking id
	input_set_abs_params(tpd->dev, ABS_MT_TRACKING_ID, 0, (CFG_MAX_TOUCH_POINTS-1), 0, 0);
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))    
    TPD_DO_WARP = 1;
    memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT*4);
    memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT*4);
#endif 

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
    memcpy(tpd_calmat, tpd_def_calmat_local, 8*4);
    memcpy(tpd_def_calmat, tpd_def_calmat_local, 8*4);	
#endif  
    DBG("end %s, %d\n", __FUNCTION__, __LINE__);  
    tpd_type_cap = 1;
    return 0; 
 }
static void fts_release_all_finger ( void )
{
	unsigned int finger_count=0;
#ifndef MT_PROTOCOL_B
	input_mt_sync ( tpd->dev );
#else
	for(finger_count = 0; finger_count < CFG_MAX_TOUCH_POINTS; finger_count++)
	{
		input_mt_slot( tpd->dev, finger_count);
		input_mt_report_slot_state( tpd->dev, MT_TOOL_FINGER, false);
	}
	input_report_key(tpd->dev, BTN_TOUCH, 0);
#endif
	input_sync ( tpd->dev );
}
static void tpd_resume( struct early_suspend *h )
{
  //int retval = TPD_OK;
  //char data;                                   //julian 5.16
  	int i;
#ifdef TPD_PROXIMITY	
	if (tpd_proximity_suspend == 0)
	{
		return;
	}
	else
	{
		tpd_proximity_suspend = 0;
	}
#endif	
 
   	DBG("TPD wake up\n");
	fts_release_all_finger();
#ifdef TPD_CLOSE_POWER_IN_SLEEP	
	hwPowerOn(TPD_POWER_SOURCE,VOL_3300,"TP");
#else

    mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
    mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);  
    msleep(5);  
   // mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
   // mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
#endif
	
	msleep(200);
#ifdef  TP_MONITOR_CTL  						    //
	mutex_lock(&i2c_access);                           //
       fts_write_reg(i2c_client, 0x86, 0x00);			
	mutex_unlock(&i2c_access);                           //
#endif  											//julian 5.16
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);  
	tpd_halt = 0;
	//fts_release_all_finger();
	/* for resume debug
	if((i2c_smbus_read_i2c_block_data(i2c_client, 0x00, 1, &data))< 0)
	{
		TPD_DMESG("resume I2C transfer error, line: %d\n", __LINE__);
	}
	*/
	/*for(i = 0; i < CFG_MAX_TOUCH_POINTS; i++)
	{
		input_mt_slot(tpd->dev, i);
		input_mt_report_slot_state(tpd->dev, MT_TOOL_FINGER, 0);
		DBG("zax TPD clear 1234.\n");
	}
	input_mt_report_pointer_emulation(tpd->dev, false);
	input_sync(tpd->dev);*/
	DBG("TPD wake up done\n");
	 //return retval;
 }

 static void tpd_suspend( struct early_suspend *h )
 {
	// int retval = TPD_OK;
	// static char data = 0x3;
	u8 state = 0, state_g = 0;
	int i = 0;
	/*for (i = 0; i <CFG_MAX_TOUCH_POINTS; i++) 
	{
		input_mt_slot(tpd->dev, i);
		input_mt_report_slot_state(tpd->dev, MT_TOOL_FINGER, 0);
		DBG("zax TPD enter sleep1234\n");
	}
	input_mt_report_pointer_emulation(tpd->dev, false);
	input_sync(tpd->dev);*/
	fts_release_all_finger();

#ifdef TPD_PROXIMITY
	if (tpd_proximity_flag == 1)
	{
		tpd_proximity_suspend = 0;	
		return;
	}
	else
	{
		tpd_proximity_suspend = 1;
	}
#endif

	tpd_halt = 2;
#ifdef FTS_GESTRUE
  printk(" gesture_enable=:%d\n",ft_wakeup_gesture);
   if(ft_wakeup_gesture){
   		mutex_lock(&i2c_access);
   #ifdef  TP_MONITOR_CTL                                //
		fts_write_reg(i2c_client, 0x86, 0x01);
    #endif			
		memset(coordinate_x,0,255);
		memset(coordinate_y,0,255);
		

       	fts_write_reg(i2c_client, 0xd0, 0x01);
		fts_write_reg(i2c_client, 0xd1, 0xff);				
		fts_write_reg(i2c_client, 0xd2, 0xff);
		fts_write_reg(i2c_client, 0xd5, 0xff);
		fts_write_reg(i2c_client, 0xd6, 0xff);
		fts_write_reg(i2c_client, 0xd7, 0xff);
		fts_write_reg(i2c_client, 0xd8, 0xff);
		mutex_unlock(&i2c_access);
		msleep(10);
		
		for(i = 0; i < 10; i++)
		{
		printk("tpd_suspend4 %d",i);
			mutex_lock(&i2c_access);
		       fts_read_reg(i2c_client, 0xd0, &state);
			mutex_unlock(&i2c_access);
			if(state == 1)
			{
				TPD_DMESG("TPD gesture write 0x01\n");
				tpd_halt = 3;
        		return;
			}
			else
			{
				mutex_lock(&i2c_access);
				fts_write_reg(i2c_client, 0xd0, 0x01);
				fts_write_reg(i2c_client, 0xd1, 0xff);				
				fts_write_reg(i2c_client, 0xd2, 0xff);
				fts_write_reg(i2c_client, 0xd5, 0xff);
				fts_write_reg(i2c_client, 0xd6, 0xff);
				fts_write_reg(i2c_client, 0xd7, 0xff);
				fts_write_reg(i2c_client, 0xd8, 0xff);
				mutex_unlock(&i2c_access);

				msleep(10);	
			  	mutex_lock(&i2c_access);
		       	fts_read_reg(i2c_client, 0xd0, &state);
				mutex_unlock(&i2c_access);
				if(state == 1)
				{
		    		TPD_DMESG("TPD gesture write 0x01 again OK\n");
					tpd_halt = 3;
					return;
	   			}
				else
				{
					mutex_lock(&i2c_access);
					fts_write_reg(i2c_client, 0xd0, 0x01);
					mutex_unlock(&i2c_access);
					msleep(10);
				}
			}
		}

			//i2c_smbus_read_i2c_block_data(i2c_client, 0xd0, 1, &state);
			//i2c_smbus_read_i2c_block_data(i2c_client, 0xd8, 1, &state_g);
			mutex_lock(&i2c_access);
			fts_read_reg(i2c_client, 0xd0, &state);
			fts_read_reg(i2c_client, 0xd8, &state_g);
			mutex_unlock(&i2c_access);
			printk("tpd fts_read_gesture ok data state=%d,d8 state_g= %d\n",state,state_g);
			tpd_halt = 1;
			return;
	}
#endif

 	 tpd_halt = 1;

	DBG("TPD enter sleep\n");
	 mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
#ifdef TPD_CLOSE_POWER_IN_SLEEP	
	hwPowerDown(TPD_POWER_SOURCE,"TP");
#else
	mutex_lock(&i2c_access);
	//i2c_smbus_write_i2c_block_data(i2c_client, 0xA5, 1, &data);  //TP enter sleep mode
	fts_write_reg(i2c_client, 0xA5, 0x03);
	mutex_unlock(&i2c_access);
#endif
	DBG("TPD enter sleep done\n");
	//return retval;
 } 


 static struct tpd_driver_t tpd_device_driver = {
	.tpd_device_name = TPD_NAME,
	.tpd_local_init = tpd_local_init,
	.suspend = tpd_suspend,
	.resume = tpd_resume,
#ifdef TPD_HAVE_BUTTON
	.tpd_have_button = 1,
#else
	.tpd_have_button = 0,
#endif		
 };
 /* called when loaded into kernel */
static int __init tpd_driver_init(void) {
	printk("MediaTek FTS touch panel driver init\n");
	i2c_register_board_info(IIC_PORT, &ft5206_i2c_tpd, 1);
//	i2c_register_board_info(0, &ft5206_i2c_tpd, 1);
	if(tpd_driver_add(&tpd_device_driver) < 0)
		TPD_DMESG("add FTS driver failed\n");
	 return 0;
 }
 
 /* should never be called */
static void __exit tpd_driver_exit(void) {
	TPD_DMESG("MediaTek FTS touch panel driver exit\n");
	//input_unregister_device(tpd->dev);
	tpd_driver_remove(&tpd_device_driver);
}
 
module_init(tpd_driver_init);
module_exit(tpd_driver_exit);
