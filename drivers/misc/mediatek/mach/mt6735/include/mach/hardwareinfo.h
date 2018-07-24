typedef struct hardware_info_struct{
char  *nand_device_name;
char  *hq_lcm_name; 
char *hq_tpd_name;
int	tpd_fw_version;
int	tpd_cfg_version;
char subCameraName[32];
char mainCameraName[32];
char *alsps_name;
char *gsensor_name;
char *msensor_name;
char *gyro_name;
unsigned int raw_cid[4];
unsigned int manfid;
} hardware_info_struct;
