#include<stdio.h>
#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <iostream>

#include "scan_rf.h"

#define LED_GREEN "/sys/class/leds/green"
#define LED_YELLOW "/sys/class/leds/yellow"

#define LED_STATUS "brightness"
#define LED_DELAY_ON "delay_on"
#define LED_DELAY_OFF "delay_off"

/*ioctl*/
#define LED_ON 1
#define LED_OFF 0

led_t green_led, yellow_led;
static char g_node_name[50];

int  led_trigger_delay(int led_id,char *on_val, char *off_val)
{
    int delay_on_fd, delay_off_fd , ret = 0;
    char buf[40];
    if(led_id == 0)
        memcpy(buf, LED_GREEN ,strlen(LED_GREEN)+1);
    else
        memcpy(buf, LED_YELLOW ,strlen(LED_YELLOW)+1);

    sprintf(g_node_name,"%s/%s", buf, LED_DELAY_ON);
    delay_on_fd = open(g_node_name, O_RDWR);
    if(delay_on_fd < 0) {
        perror("led_trigger_delay open");
        return -1;
    }
    ret = write(delay_on_fd, on_val, strlen(on_val));
    close(delay_on_fd);

    sprintf(g_node_name,"%s/%s", buf, LED_DELAY_OFF);
    delay_off_fd = open(g_node_name, O_RDWR);
    if(delay_off_fd < 0) {
        perror("led_trigger_delay open");
        return -1 ;
    }
    ret = write(delay_off_fd, off_val, strlen(off_val));
    close(delay_off_fd);

    return 0;
}


int  led_bright_ctl(int led_fd, unsigned int on)
{
    int ret;
    if(led_fd <= 0){
        perror("write led fd error");
        return -1 ;
    }

    if(on == 0){
        ret = write(led_fd, "0", 2);
    }else{
        ret = write(led_fd, "1", 2); //255
    }

    if(ret < 0) {
        perror("write led error");
        return -1 ;
    }
}

int led_trigger_ctl(int led_trigger_fd,const char *trigger_type)
{
    int ret;

    ret = write(led_trigger_fd, trigger_type, strlen(trigger_type));
    if(ret < 0) {
        perror("write led trigger error");
        return -1;
    }

#if 0
    if(strcmp(trigger_type,TRIGGER_TIMER) == 0)
    {
         printf("trigger type(%s), len =%d\n",trigger_type,strlen(trigger_type));
         led_trigger_delay(1,"800","200");
    }else{

        printf("trigger type len =%d ,none?\n",strlen(trigger_type));
    }
#endif

}

int gpio_ctl_out(const char *gpio_num, const char *out_val)
{
    int valuefd, exportfd, directionfd;
    char dir[60],val[60];
    // The GPIO has to be exported to be able to see it
    // in sysfs

    exportfd = open("/sys/class/gpio/export", O_WRONLY);
    if (exportfd < 0)
    {
        printf("Cannot open GPIO to export it\n");
        return -1;
    }

    write(exportfd, gpio_num, 4);
    close(exportfd);

    printf("GPIO exported successfully\n");

    // Update the direction of the GPIO to be an output
    //"/sys/class/gpio/gpio414/direction"
    sprintf(dir, "%s%s%s", "/sys/class/gpio/gpio", gpio_num, "/direction");
    directionfd = open(dir, O_RDWR);
    if (directionfd < 0)
    {
        printf("Cannot open GPIO direction\n");
        return -1;
    }

    write(directionfd, "out", 4);
    close(directionfd);

    printf("GPIO direction set as output successfully\n");

    // Get the GPIO value ready to be toggled
    sprintf(val, "%s%s%s", "/sys/class/gpio/gpio", gpio_num, "/value");
    valuefd = open(val, O_RDWR);
    if (valuefd < 0)
    {
        printf("Cannot open GPIO value\n");
        return -1;
    }
    write(valuefd, out_val, 2);
    close(valuefd);

}

void gpio_led_exit(void)
{
    if(green_led.bright_fd != 0){
        close(green_led.bright_fd);
        green_led.bright_fd = 0;
    }

    if(green_led.trigger_fd != 0){
        close(green_led.trigger_fd);
        green_led.trigger_fd = 0;
    }

    if(yellow_led.bright_fd != 0){
        close(yellow_led.bright_fd);
        yellow_led.bright_fd = 0;
    }

    if(yellow_led.trigger_fd != 0){
        close(yellow_led.trigger_fd);
        yellow_led.trigger_fd = 0;
    }

}

void gpio_led_init(void)
{
    FILE *fp;
    int val, ret= 0 ;
    char *tmp,buf[200];
    tmp = buf;

    green_led.led_id = 0;
    sprintf(g_node_name,"%s/%s", LED_GREEN, "brightness");
    green_led.bright_fd = open(g_node_name, O_RDWR);
    if(green_led.bright_fd < 0) {
        perror("bright_fd open");
        return ;
    }

    sprintf(g_node_name,"%s/%s", LED_GREEN, "trigger");
    green_led.trigger_fd = open(g_node_name, O_RDWR);
    if(green_led.trigger_fd < 0) {
        perror("green_led.trigger_fd  open error");
        return ;
    }

    yellow_led.led_id = 1;
    sprintf(g_node_name,"%s/%s", LED_YELLOW, "brightness");
    yellow_led.bright_fd = open(g_node_name, O_RDWR);
    if(yellow_led.bright_fd < 0) {
        perror("bright_fd open");
        return ;
    }

    sprintf(g_node_name,"%s/%s", LED_YELLOW, "trigger");
    yellow_led.trigger_fd = open(g_node_name, O_RDWR);
    if(yellow_led.trigger_fd < 0) {
        perror("green_led.trigger_fd  open error");
        return ;
    }

}


