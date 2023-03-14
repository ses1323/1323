#ifndef WIFICONFIG_H_
#define WIFICONFIG_H_

#define DONGLE_FILE     "/run/media/mmcblk0p1/dongle.bin"
#define MFG_UPDATE_FILE "mfg_update.ini"
#define REGION_CUR_FILE "region_cur.ini"
#define TTCODE_FILE     "ttcode.ini"

unsigned int ttcode_read(void);
int ttcode_write(int len, unsigned int val);
int ttcode_init(void);
int ttcode_file_close(void);

unsigned int config_file_read(const char *fname,char *buf, unsigned int *ret_len);
int config_file_write(const char *fname,char* buf,int len);
FILE *config_file_init(const char *fname);
int config_file_close(FILE *fp);

int wifi_ap_start(void);
int wifi_sta_start(void);
int wifi_stop(void  );
int temperature_init(void);
float get_xadc_val(int temp_type);
int set_sync_time(unsigned int *time_buf,int buf_len);


#endif
