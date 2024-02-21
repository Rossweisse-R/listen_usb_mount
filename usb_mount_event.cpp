#include <iostream>
#include <array>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

enum{
    MOUNT_UDEV,
    NO_UDEV
};

static struct sockaddr_nl client;
static int uevent_fd = 0;
int usb_status = -1;
std::string mount_usb_dir = "";
std::vector <std::string> block_list;

static std::string exec(const char* cmd) {
    std::array<char, 1024*4> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

static int init_uevent_socket(){
    int buffersize = 1024;
    int uevent_fd;
    uevent_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if(uevent_fd < 0){
        std::cout << "[usb_mount_event]\tusb event socket error" << std::endl;
        return -1;
    }
    memset(&client, 0, sizeof(client));
    client.nl_family = AF_NETLINK;
    client.nl_pid = getpid();
    client.nl_groups = 1;
    setsockopt(uevent_fd, SOL_SOCKET, SO_RCVBUF, &buffersize, sizeof(buffersize));
    int ret = bind(uevent_fd, (struct sockaddr*)&client, sizeof(client));
    if(ret < 0){
        std::cout << "[usb_mount_event]\tusb event bind error" << std::endl;
        close(uevent_fd);
        return -1;
    }
    return uevent_fd;
}

static void analysis_udev(char *str){
    char action[50] = {0};
    //sscanf()中,%[a-z]表示匹配包含a-z之间的字符串;%[^a-z]表示匹配不包含a-z之间的字符串;%06[a-z]表示这个字符串缓冲区的最大长度;%*[0-9]表示忽略包含0-9字符串的数据
    sscanf(str,"%[a-z]@/",action);
    if(!strcmp(action,"add")){
        std::string str_tmp = str;
        std::string::size_type pos = str_tmp.rfind("/block/");
        if(pos != std::string::npos){
            std::string str_buf = str_tmp.substr(pos + 7,(str_buf.size()-pos-7));
            std::string::size_type pos_tmp = str_buf.rfind("/");
            if(pos_tmp != std::string::npos){
                usb_status = MOUNT_UDEV;
                mount_usb_dir = str_buf.substr(pos_tmp + 1,(str_buf.size() - pos_tmp - 1));
                std::cout<<"[usb_mount_event]\tU盘插入,挂载到目录"<< mount_usb_dir << std::endl;
            }
        }
    }else if(!strcmp(action, "remove")){
        std::string str_tmp = str;
        std::string::size_type pos = str_tmp.rfind("/block/");
        if(pos != std::string::npos){
            std::string str_buf = str_tmp.substr(pos + 7,(str_buf.size()-pos-7));
            std::string::size_type pos_tmp = str_buf.rfind("/");
            if(pos_tmp != std::string::npos){
                usb_status = NO_UDEV;
                if(mount_usb_dir == str_buf.substr(pos_tmp + 1,(str_buf.size() - pos_tmp - 1))){
                    mount_usb_dir.clear();
                }
                std::cout<<"[usb_mount_event]\tU盘移除,移除目录为:"<< str_buf.substr(pos_tmp,(str_buf.size() - pos_tmp)) << std::endl;
            }
        }
    }
}

void *check_usb_status(void *p){
    uevent_fd = init_uevent_socket();
    if(uevent_fd < 0){
        return NULL;
    }
    char buf[2048] = {0};

    std::string ret = exec("ls /dev/sd*");
    //std::cout<<"[usb_mount_event]\texec执行结果为 \""<<ret<<"\""<<std::endl;
    if(ret == ""){
        std::cout<<"[usb_mount_event]\t并未插入U盘"<<std::endl;
        usb_status = NO_UDEV;
        mount_usb_dir.clear();
    }else{
        usb_status = MOUNT_UDEV;
        while(1){
            std::string::size_type pos = ret.find("/sd");
            if(pos == std::string::npos){
                break;
            }
            std::string device_mount_dir = ret.substr(pos+1,5);
            ret.replace(0,pos+5,"");
            if(device_mount_dir.back() == '/'){//如果最后一个字符是'/',舍弃
                device_mount_dir.replace(device_mount_dir.size()-1,device_mount_dir.size(),"");
            }
            if(device_mount_dir.back() == 10){//ret最后一个字符是回车,应舍弃
                device_mount_dir.replace(device_mount_dir.size()-1,device_mount_dir.size(),"");
            }
            //std::cout<<"[usb_mount_event]\t已经挂载的路径为"<<device_mount_dir<<std::endl;
            block_list.push_back(device_mount_dir);
        }
        std::cout<<"扫描到挂载中的usb设备,遍历block_list"<<std::endl;
        for(auto i:block_list){
            std::cout << i <<std::endl;
        }
    }

    std::cout<<"[usb_mount_event]\tU盘挂载状态监听开始"<<std::endl;
    switch (usb_status)
    {
    case MOUNT_UDEV:
        std::cout<<"[usb_mount_event]\t当前usb_status = MOUNT_UDEV,已经挂载U盘"<<std::endl;
        break;
    case NO_UDEV:
        std::cout<<"[usb_mount_event]\t当前usb_status = NO_UDEV,并无U盘挂载"<<std::endl;
        break;
    default:
        break;
    }
    while(1){
        memset(buf,0,sizeof(buf));
        recv(uevent_fd,&buf,sizeof(buf),0);
        //printf("[发生usb事件]\t%s\n",buf);
        analysis_udev(buf);
    }
}

int main(int argc, char const *argv[])
{
    std::cout<<"Listen Usb Mount Event,Start"<<std::endl;
    while(1){
        check_usb_status(NULL);
    }
    return 0;
}
