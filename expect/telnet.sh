#!/usr/bin/expect -f
          set timeout 5
spawn telnet 192.168.3.10
expect "wifi-demo login:"  
     send "root\r" 
expect "Password:" 
     send "root@123\r"
expect "#" 
     send "ls\r"
expect "#"
     send "cat /run/media/mmcblk0p1/vinno/wpa_supplicant_ap.conf\r"
interact
