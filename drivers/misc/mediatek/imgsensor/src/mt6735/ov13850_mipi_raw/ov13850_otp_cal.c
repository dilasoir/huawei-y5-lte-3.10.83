#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/xlog.h>
//#include <asm/system.h>
#include <linux/proc_fs.h> 
#include <linux/dma-mapping.h>
#include "kd_camera_hw.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"
#include "ov13850mipiraw_Sensor.h"
#include "ov13850_otp.h"
extern int iReadReg(u16 a_u2Addr , u8 * a_puBuff , u16 i2cId);
extern int iWriteReg(u16 a_u2Addr , u32 a_u4Data , u32 a_u4Bytes , u16 i2cId);
//#define OV13850_R2A_write_i2c(addr, para) iWriteReg((u16) addr , (u32) para , 1, OV13850MIPI_WRITE_ID)
#define PFX "OV13850_R2A_OTP"
#define LOG_INF(format, args...)	//xlog_printk(ANDROID_printkO   , PFX, "[%s] " format, __FUNCTION__, ##args)
#define Delay(ms)  mdelay(ms)
static unsigned char OV13850MIPI_WRITE_ID = 0X00;
int read_otp_date_once_flag = 0;//add by liyongjian 
static kal_uint16 OV13850_R2A_read_i2c(kal_uint32 addr)
{
    kal_uint16 get_byte=0;
    iReadReg((u16) addr ,(u8*)&get_byte,OV13850MIPI_WRITE_ID);
    return get_byte;
}
static kal_uint16 OV13850_R2A_write_i2c(addr, para)
{
		iWriteReg((u16) addr , (u32) para , 1, OV13850MIPI_WRITE_ID);
		return 1;
}
static int Decode_13850R2A(unsigned char*pInBuf, unsigned char* pOutBuf)
{
	if(pInBuf != NULL)
	{
		LumaDecoder(pInBuf, pOutBuf);
		ColorDecoder((pInBuf+86), (pOutBuf+120));
		ColorDecoder((pInBuf+136), (pOutBuf+240));
		printk(" OTP OK \n");
		return 1;
	}else
	{
		printk(" OTP FAIL \n");
		return 0;	
	}
}
// return value:
// bit[7]: 0 no otp info, 1 valid otp info
// bit[6]: 0 no otp wb, 1 valib otp wb
// bit[5]: 0 no otp vcm, 1 valid otp vcm
// bit[4]: 0 no otp lenc, 1 valid otp lenc
int OV13850_read_otp(struct otp_struct *otp_ptr,unsigned char writeid)
{
	OV13850MIPI_WRITE_ID = writeid;
	int otp_flag=0;
	int addr=0;
	int temp=0;
	int i=0;
	int checksumLSC = 0;
	int checksumOTP = 0;
	int checksumTotal = 0;
	unsigned char lenc_out[360];
	int temp1=0;
	temp1 = OV13850_R2A_read_i2c(0x5002);
	OV13850_R2A_write_i2c(0x5002, (0x00 & 0x02) | (temp1 & (~0x02)));
	// read OTP into buffer
	OV13850_R2A_write_i2c(0x3d84, 0xC0);
	OV13850_R2A_write_i2c(0x3d88, 0x72); // OTP start address
	OV13850_R2A_write_i2c(0x3d89, 0x20);
	OV13850_R2A_write_i2c(0x3d8A, 0x73); // OTP end address
	OV13850_R2A_write_i2c(0x3d8B, 0xBE);
	OV13850_R2A_write_i2c(0x3d81, 0x01); // load otp into buffer
	Delay(10);
	// OTP base information and WB calibration data
	otp_flag = OV13850_R2A_read_i2c(0x7220);
	addr = 0;
	if((otp_flag & 0xc0) == 0x40) {
		addr = 0x7221; // base address of info group 1
	}
	else if((otp_flag & 0x30) == 0x10) {
		addr = 0x7229; // base address of info group 2
	}
	if(addr != 0 ) {
		(*otp_ptr).flag = 0xC0; // valid info and AWB in OTP
		(*otp_ptr).module_integrator_id = OV13850_R2A_read_i2c(addr);
		(*otp_ptr).lens_id = OV13850_R2A_read_i2c( addr + 1);
		(*otp_ptr).awb_golden_lsb1 = OV13850_R2A_read_i2c(addr + 4);
		(*otp_ptr).awb_golden_RG_msb1 = 0x13E;//(OV13850_R2A_read_i2c( addr + 2)<<2) + (((*otp_ptr).awb_golden_lsb1>>6) & 0x03);
		(*otp_ptr).awb_golden_BG_msb1 = 0x150;//(OV13850_R2A_read_i2c( addr + 3)<<2) + (((*otp_ptr).awb_golden_lsb1>>4) & 0x03);		
		temp = OV13850_R2A_read_i2c(addr + 7);
		(*otp_ptr).rg_ratio = (OV13850_R2A_read_i2c(addr + 5)<<2) + ((temp>>6) & 0x03);
		(*otp_ptr).bg_ratio = (OV13850_R2A_read_i2c(addr + 6)<<2) + ((temp>>4) & 0x03);	
	}
	else {
		printk("====zpc==empty=====\n");
		(*otp_ptr).flag = 0x00; // not info in OTP
		(*otp_ptr).module_integrator_id = 0;
		(*otp_ptr).lens_id = 0;
		(*otp_ptr).awb_golden_RG_msb1 = 0;
		(*otp_ptr).awb_golden_BG_msb1 = 0;
		(*otp_ptr).awb_golden_lsb1 = 0;
	}
	// OTP Lenc Calibration
	otp_flag = OV13850_R2A_read_i2c(0x7231);
	addr = 0;
	if((otp_flag & 0xc0) == 0x40) {
		addr = 0x7232; // base address of Lenc Calibration group 1
	}
	else if((otp_flag & 0x30) == 0x10) {
		addr = 0x72ef; // base address of Lenc Calibration group 2
	}
	if(addr != 0 ) {
		for(i=0;i<186;i++) {
			(* otp_ptr).lenc[i]= OV13850_R2A_read_i2c(addr + i);
			checksumLSC += (* otp_ptr).lenc[i];
		}
		for(i=0;i<360;i++)
		{
			lenc_out[i] = 0;
		}
		if(Decode_13850R2A((*otp_ptr).lenc,  lenc_out))
		{
			for(i=0;i<360;i++) 
			{
				checksumOTP = checksumOTP + lenc_out[i]; 
			}
		}
		checksumLSC = (checksumLSC)%255 +1;	
		checksumOTP = (checksumOTP)%255 +1;
		checksumTotal = (checksumLSC) ^ (checksumOTP);
		(* otp_ptr).checksumLSC=OV13850_R2A_read_i2c(addr + 186);
		(* otp_ptr).checksumOTP=OV13850_R2A_read_i2c(addr + 187);
		(* otp_ptr).checksumTotal=OV13850_R2A_read_i2c(addr + 188);
		if((* otp_ptr).checksumLSC == checksumLSC && (* otp_ptr).checksumOTP == checksumOTP){
			(*otp_ptr).flag |= 0x10;
		}
		else if((* otp_ptr).checksumTotal == checksumTotal){
			(*otp_ptr).flag |= 0x10;
		}
	}
	else {
		for(i=0;i<186;i++) {
			(* otp_ptr).lenc[i]=0;
		}
	}
	for(i=0x7220;i<=0x73be;i++) 
	{		
		OV13850_R2A_write_i2c(i,0); // clear OTP buffer, recommended use continuous write to accelarate
	}
	read_otp_date_once_flag = 0;
	//set 0x5002[1] to "1"
	temp1 = OV13850_R2A_read_i2c(0x5002);
	OV13850_R2A_write_i2c(0x5002, (0x02 & 0x02) | (temp1 & (~0x02)));
	return (*otp_ptr).flag;
}
// return value:
// bit[7]: 0 no otp info, 1 valid otp info
// bit[6]: 0 no otp wb, 1 valib otp wb
// bit[5]: 0 no otp vcm, 1 valid otp vcm
// bit[4]: 0 no otp lenc, 1 valid otp lenc
 int OV13850_apply_otp(struct otp_struct *otp_ptr)
{
	int rg=0;
	int bg=0;
	int R_gain=0;
	int G_gain=0;
	int B_gain=0;
	int Base_gain=0;
	int temp=0;
	int i=0;
	unsigned char lenc_out[360];
	// apply OTP WB Calibration
	if ((*otp_ptr).flag & 0x40) {
		rg = (*otp_ptr).rg_ratio;
		bg = (*otp_ptr).bg_ratio;
		printk("lili>>start cali awb rg=%d ,bg=%d,golden_rg=%d ,golden_bg=%d\n",rg , bg,(*otp_ptr).awb_golden_RG_msb1 , (*otp_ptr).awb_golden_BG_msb1);
		//calculate G gain
		R_gain = ((*otp_ptr).awb_golden_RG_msb1*1000) / rg;
		B_gain = ((*otp_ptr).awb_golden_BG_msb1*1000) / bg;
		G_gain = 1000;

		if (R_gain < 1000 || B_gain < 1000)
		{
			if (R_gain < B_gain)
				Base_gain = R_gain;
			else
				Base_gain = B_gain;
		}
		else
		{
			Base_gain = G_gain;
		}
		R_gain = 0x400 * R_gain / (Base_gain);
		B_gain = 0x400 * B_gain / (Base_gain);
		G_gain = 0x400 * G_gain / (Base_gain);
		printk("lili>>>>after cali R_gain=0x%x,B_gain=0x%x,G_gain=0x%x\n",R_gain,B_gain,G_gain);
		// update sensor WB gain
		if (R_gain>0x400) {
			OV13850_R2A_write_i2c(0x5056, R_gain>>8);
			OV13850_R2A_write_i2c(0x5057, R_gain & 0x00ff);
		}
		if (G_gain>0x400) {
			OV13850_R2A_write_i2c(0x5058, G_gain>>8);
			OV13850_R2A_write_i2c(0x5059, G_gain & 0x00ff);
		}
		if (B_gain>0x400) {
			OV13850_R2A_write_i2c(0x505A, B_gain>>8);
			OV13850_R2A_write_i2c(0x505B, B_gain & 0x00ff);
		}
	}
	// apply OTP Lenc Calibration
	if ((*otp_ptr).flag & 0x10) {		
		printk(" apply OTP Lenc Calibration : %x \n", (*otp_ptr).flag);
		temp = OV13850_R2A_read_i2c(0x5000);
		temp = 0x01 | temp;
		OV13850_R2A_write_i2c(0x5000, temp);		
		for(i=0;i<360;i++)
		{
			lenc_out[i] = 0;
		}
		/***For function Decode_13850R2A(unsigned char*pInBuf, unsigned char* pOutBuf),please refer to lc42.h***/
		if(Decode_13850R2A((*otp_ptr).lenc,  lenc_out))
		{
			for(i=0;i<360 ;i++) {
				OV13850_R2A_write_i2c(0x5200 + i, lenc_out[i]);
			}
			printk("lili>>>lsc cali success\n");
		}
	}
}

/***********Decode LENC Para Process Start*********************/
static void LumaDecoder(uint8_t *pData, uint8_t *pPara)
{
	
	uint32_t Offset, Bit, Option;
	uint32_t i, k;
	uint8_t pCenter[16], pMiddle[32], pCorner[72];
	Offset = pData[0];
	Bit = pData[1]>>4;
	Option = pData[1]&0xf;
	printk("Offset:%x, Bit:%x, Option:%x \n",Offset, Bit, Option);
	for (i=0; i<180; i++)
	{
		printk("data-pData[%d]:%x \n", i, pData[i]);
	}
	if(Bit <= 5)
	{
		for(i=0,k=2; i<120; i+=8,k+=5)
		{
			//printk("pData[%d]:%x \n", i, pData[i]);
			pPara[i] = pData[k]>>3; // 7~3 (byte0)
			pPara[i+1] = ((pData[k]&0x7)<<2)|(pData[k+1]>>6); // 2~0 (byte0) and 7~6(byte1)
			pPara[i+2] = (pData[k+1]&0x3e)>>1; // 5~1 (byte1)
			pPara[i+3] = ((pData[k+1]&0x1)<<4)|(pData[k+2]>>4); // 0 (byte1) and 7~4(byte2)
			pPara[i+4] = ((pData[k+2]&0xf)<<1)|(pData[k+3]>>7); // 3~0 (byte2) and 7(byte3)
			pPara[i+5] = (pData[k+3]&0x7c)>>2; // 6~2 (byte3)
			pPara[i+6] = ((pData[k+3]&0x3)<<3)|(pData[k+4]>>5); // 1~0 (byte3) and 7~5(byte4)
			pPara[i+7] = pData[k+4]&0x1f; // 4~0 (byte4)
		}
	}
	else
	{
		for(i=0,k=2; i<48; i+=8,k+=5)
		{
			//printk(" apData[%d]:%x \n", i, pData[i]);
			pPara[i] = pData[k]>>3; // 7~3 (byte0)
			pPara[i+1] = ((pData[k]&0x7)<<2)|(pData[k+1]>>6); // 2~0 (byte0) and 7~6(byte1)
			pPara[i+2] = (pData[k+1]&0x3e)>>1; // 5~1 (byte1)
			pPara[i+3] = ((pData[k+1]&0x1)<<4)|(pData[k+2]>>4); // 0 (byte1) and 7~4(byte2)
			pPara[i+4] = ((pData[k+2]&0xf)<<1)|(pData[k+3]>>7); // 3~0 (byte2) and 7(byte3)
			pPara[i+5] = (pData[k+3]&0x7c)>>2; // 6~2 (byte3)
			pPara[i+6] = ((pData[k+3]&0x3)<<3)|(pData[k+4]>>5); // 1~0 (byte3) and 7~5(byte4)
			pPara[i+7] = pData[k+4]&0x1f; // 4~0 (byte4)
		}
		for(i=48,k=32; i<120; i+=4,k+=3)
		{
			//printk(" 48--pData[%d]:%x \n", i, pData[i]);
			pPara[i] = pData[k]>>2; // 7~2 (byte0)
			pPara[i+1] = ((pData[k]&0x3)<<4)|(pData[k+1]>>4); //1~0 (byte0) and7~4(byte1)
			pPara[i+2] = ((pData[k+1]&0xf)<<2)|(pData[k+2]>>6); //3~0 (byte1) and7~6(byte2)
			pPara[i+3] = pData[k+2]&0x3f; // 5~0 (byte2)
		}
		memcpy(pCenter, pPara, 16);
		memcpy(pMiddle, pPara+16, 32);
		memcpy(pCorner, pPara+48, 72);
		for(i=0; i<32; i++)
		{
			pMiddle[i] <<= (Bit-6);
		}
		for(i=0; i<72; i++)
		{
			pCorner[i] <<= (Bit-6);
		}
	if(Option == 0)
	{ // 10x12
		memcpy(pPara, pCorner, 26);
		memcpy(pPara+26, pMiddle, 8);
		memcpy(pPara+34, pCorner+26, 4);
		memcpy(pPara+38, pMiddle+8, 2);
		memcpy(pPara+40, pCenter, 4);
		memcpy(pPara+44, pMiddle+10, 2);
		memcpy(pPara+46, pCorner+30, 4);
		memcpy(pPara+50, pMiddle+12, 2);
		memcpy(pPara+52, pCenter+4, 4);
		memcpy(pPara+56, pMiddle+14, 2);
		memcpy(pPara+58, pCorner+34, 4);
		memcpy(pPara+62, pMiddle+16, 2);
		memcpy(pPara+64, pCenter+8, 4);
		memcpy(pPara+68, pMiddle+18, 2);
		memcpy(pPara+70, pCorner+38, 4);
		memcpy(pPara+74, pMiddle+20, 2);
		memcpy(pPara+76, pCenter+12, 4);
		memcpy(pPara+80, pMiddle+22, 2);
		memcpy(pPara+82, pCorner+42, 4);
		memcpy(pPara+86, pMiddle+24, 8);
		memcpy(pPara+94, pCorner+46, 26);
	}
	else
	{ // 12x10
		memcpy(pPara, pCorner, 22);
		memcpy(pPara+22, pMiddle, 6);
		memcpy(pPara+28, pCorner+22, 4);
		memcpy(pPara+32, pMiddle+6, 6);
		memcpy(pPara+38, pCorner+26, 4);
		memcpy(pPara+42, pMiddle+12, 1);
		memcpy(pPara+43, pCenter, 4);
		memcpy(pPara+47, pMiddle+13, 1);
		memcpy(pPara+48, pCorner+30, 4);
		memcpy(pPara+52, pMiddle+14, 1);
		memcpy(pPara+53, pCenter+4, 4);
		memcpy(pPara+57, pMiddle+15, 1);
		memcpy(pPara+58, pCorner+34, 4);
		memcpy(pPara+62, pMiddle+16, 1);
		memcpy(pPara+63, pCenter+8, 4);
		memcpy(pPara+67, pMiddle+17, 1);
		memcpy(pPara+68, pCorner+38, 4);
		memcpy(pPara+72, pMiddle+18, 1);
		memcpy(pPara+73, pCenter+12, 4);
		memcpy(pPara+77, pMiddle+19, 1);
		memcpy(pPara+78, pCorner+42, 4);
		memcpy(pPara+82, pMiddle+20, 6);
		memcpy(pPara+88, pCorner+46, 4);
		memcpy(pPara+92, pMiddle+26, 6);
		memcpy(pPara+98, pCorner+50, 22);
	}
 }
	for(i=0; i<120; i++)
	{
		pPara[i] += Offset;
	}

}
//
static void ColorDecoder(uint8_t *pData, uint8_t *pPara)
{
	
	uint32_t Offset, Bit, Option;
	uint32_t i, k;
	uint8_t pBase[30];
	Offset = pData[0];
	Bit = pData[1]>>7;
	Option = (pData[1]&0x40)>>6;
	pPara[0] = (pData[1]&0x3e)>>1; // 5~1 (byte1)
	pPara[1] = ((pData[1]&0x1)<<4)|(pData[2]>>4); // 0 (byte1) and 7~4 (byte2)
	pPara[2] = ((pData[2]&0xf)<<1)|(pData[3]>>7); // 3~0 (byte2) and 7 (byte3)
	pPara[3] = (pData[3]&0x7c)>>2; // 6~2 (byte3)
	pPara[4] = ((pData[3]&0x3)<<3)|(pData[4]>>5); // 1~0 (byte3) and 7~5 (byte4)
	pPara[5] = pData[4]&0x1f; // 4~0 (byte4)
	for(i=6,k=5; i<30; i+=8,k+=5)
	{
		pPara[i] = pData[k]>>3; // 7~3 (byte0)
		pPara[i+1] = ((pData[k]&0x7)<<2)|(pData[k+1]>>6); // 2~0 (byte0) and 7~6 (byte1)
		pPara[i+2] = (pData[k+1]&0x3e)>>1; // 5~1 (byte1)
		pPara[i+3] = ((pData[k+1]&0x1)<<4)|(pData[k+2]>>4); // 0 (byte1) and 7~4 (byte2)
		pPara[i+4] = ((pData[k+2]&0xf)<<1)|(pData[k+3]>>7); // 3~0 (byte2) and 7 (byte3)
		pPara[i+5] = (pData[k+3]&0x7c)>>2; // 6~2 (byte3)
		pPara[i+6] = ((pData[k+3]&0x3)<<3)|(pData[k+4]>>5); // 1~0 (byte3) and 7~5 (byte4)
		pPara[i+7] = pData[k+4]&0x1f; // 4~0 (byte4)
	}
	memcpy(pBase, pPara, 30);
	for(i=0,k=20; i<120; i+=4,k++)
	{
		pPara[i] = pData[k]>>6;
		pPara[i+1] = (pData[k]&0x30)>>4;
		pPara[i+2] = (pData[k]&0xc)>>2;
		pPara[i+3] = pData[k]&0x3;
	}
	if(Option == 0)
	{ // 10x12
		for(i=0; i<5; i++)
		{
			for(k=0; k<6; k++)
			{
				pPara[i*24+k*2] += pBase[i*6+k];
				pPara[i*24+k*2+1] += pBase[i*6+k];
				pPara[i*24+k*2+12] += pBase[i*6+k];
				pPara[i*24+k*2+13] += pBase[i*6+k];
			}
		}
	}else
	{ // 12x10
		for(i=0; i<6; i++)
		{
			for(k=0; k<5; k++)
			{
				pPara[i*20+k*2] += pBase[i*5+k];
				pPara[i*20+k*2+1] += pBase[i*5+k];
				pPara[i*20+k*2+10] += pBase[i*5+k];
				pPara[i*20+k*2+11] += pBase[i*5+k];
			}
		}
	}
	for(i=0; i<120; i++)
	{
		pPara[i] = (pPara[i]<<Bit) + Offset;
	}
	
}
//
