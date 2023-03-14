#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <list>
#include <vector>
#include <time.h>
#include <iostream>
#include <dirent.h>
#include <sys/wait.h>
#include "scan_rf.h"
// #include <signal.h>
#include <sys/prctl.h>
// #include <csignal>
#include <algorithm>

using namespace std;
#define LOGMAX 5
int log_count=0;
string getTime()
{
    time_t tt = time(NULL);
    struct tm *stm = localtime(&tt);
    char tmp[32];
    sprintf(tmp, "%04d-%02d-%02d-%02d-%02d-%02d", 1900 + stm->tm_year, 1 + stm->tm_mon, stm->tm_mday, stm->tm_hour,
            stm->tm_min, stm->tm_sec);
    return tmp;
}
//时间字符串排序
bool compare(string tm1, string tm2)
{
    struct tm tm_time1;
    struct tm tm_time2;
    time_t retval1 = 0;
    time_t retval2 = 0;
    strptime(tm1.c_str(), "%Y-%m-%d-%H-%M-%S", &tm_time1);
    strptime(tm2.c_str(), "%Y-%m-%d-%H-%M-%S", &tm_time2);
    retval1 = mktime(&tm_time1);
    retval2 = mktime(&tm_time2);
    if (retval1 > retval2)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//管理vinnolog 超过一定数量删除早的log
void manage_vinnolog()
{
    DIR *dp;
    vector<string> log_file_list;
    log_file_list.clear();
    struct dirent *dirp;
    string dir_name = "/run/media/mmcblk0p1/log/";
    string file_name = "";
    string key_words = "vinnologs";
    string cur_time = getTime();
    log_file_list.push_back(cur_time);
    if ((dp = opendir(dir_name.c_str())) == NULL)
    {
        perror("opendir");
        return;
    }
    while ((dirp = readdir(dp)) != NULL)
    {
        file_name = dirp->d_name;
        string::size_type position;
        position = file_name.find(key_words);
        int key_words_len = key_words.length() + 1;
        int file_name_len = file_name.length();
        if ((position != file_name.npos) && (key_words_len < file_name_len))
        {
            // printf("%s\n",fileName );
            string file_name_date = file_name.substr(key_words_len, file_name_len);
            log_file_list.push_back(file_name_date);
        }
    }
    if ((log_file_list.size() - 1) >= LOGMAX)
    {
        sort(log_file_list.begin(), log_file_list.end(), compare);
        int id = find(log_file_list.begin(), log_file_list.end(), cur_time) - log_file_list.begin();
        for (int a = 0; a < id; a++)
        {
            cout << "vinnologs-" + log_file_list[a] << endl;
            string rm_str = "rm /run/media/mmcblk0p1/log/vinnologs-" + log_file_list[a];
            system(rm_str.c_str());
        }
        if ((id + LOGMAX) < log_file_list.size())
        {
            for (int b = (id + LOGMAX); b < log_file_list.size(); b++)
            {
                cout << "vinnologs-" + log_file_list[b] << endl;
                string rm_str = "rm /run/media/mmcblk0p1/log/vinnologs-" + log_file_list[b];
                system(rm_str.c_str());
            }
        }
        // for(int i=(LOGMAX-1);i<log_file_list.size();i++)
        // {
        //     cout << "vinnologs-"+log_file_list[i] << endl;
        //     string rm_str = "rm /run/media/mmcblk0p1/log/vinnologs-" + log_file_list[i];
        //     system(rm_str.c_str());
        // }
        system("sync");
    }
    log_file_list.clear();
    closedir(dp);
}
//创建log
int creat_vinnolog(string log_name)
{
    FILE *fp;
    if ((access(log_name.c_str(), F_OK)) != -1)
    {
        if (remove(log_name.c_str()) == 0)
        {
            fp = fopen(log_name.c_str(), "w+");
            if (fp == NULL)
            {
                return -1;
            }
            else
            {
                fclose(fp);
                return 0;
            }
        }
    }
    else
    {
        fp = fopen(log_name.c_str(), "w+");
        if (fp == NULL)
        {
            return -1;
        }
        else
        {
            fclose(fp);
            return 0;
        }
    }
}

void backup_vinnolog()
{
    string log_name;
    log_name = "/run/media/mmcblk0p1/log/vinnologs-" + getTime();
    if (creat_vinnolog(log_name) == 0)
    {
        system("sync");
    }
    return ;
}

void start_vinnolog()
{
    // check time
    FILE *fp;
    char buf[225];
    pid_t pid = -1;
    char cmd[] = "pidof tail";

    if ((fp = popen(cmd, "r")) != NULL)
    {
        if (fgets(buf, 225, fp) != NULL)
        {
            pid = atoi(buf);
            cout << "vinnolog pid is:" << pid << endl;
            log_count++;
        }
        else
        {
            if (log_count == 0)
            {
                manage_vinnolog();
                backup_vinnolog();
            }
        }
        pclose(fp);
    }
        // cout << "tail -f -n 50 /var/log/vinnolog " << endl;
}
