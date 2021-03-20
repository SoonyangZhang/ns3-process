/*
Author: zsy  
Create: 2021/03/19  
*/
#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <memory.h>
#include <stdlib.h>
#include <vector>
#include <deque>
#include <set>
#include <limits>
#include "ns3/log.h"
#include "ns3/ping-pong-module.h"
static volatile bool g_running=true;
static void abc_signal_handler(int signo, siginfo_t *siginfo, void *ucontext){
    switch(signo){
        case abc_signal_value(ABC_SHUTDOWN_SIGNAL):
        case abc_signal_value(ABC_TERMINATE_SIGNAL):
        case SIGINT:
        case SIGHUP:
        case SIGTSTP:
            g_running=false;
            std::cout<<"signal "<<signo<<std::endl;
            break;
        default:
            break;
    }
}
abc_signal_t  signals[] ={
    { abc_signal_value(ABC_TERMINATE_SIGNAL),
      (char*)"SIG" abc_str_value(ABC_TERMINATE_SIGNAL),
      (char*)"stop",
      abc_signal_handler},
    {abc_signal_value(ABC_SHUTDOWN_SIGNAL),
      (char*)"SIG" abc_str_value(ABC_SHUTDOWN_SIGNAL),
      (char*)"quit",
      abc_signal_handler},
    { SIGINT, (char*)"SIGINT", (char*)"", abc_signal_handler },
    { SIGHUP, (char*)"SIGHUP", (char*)"", abc_signal_handler },
    { SIGTSTP, (char*)"SIGTSTP",(char*)"", abc_signal_handler },
    { 0, NULL, (char*)"", NULL}
};
int abc_init_signals()
{
    abc_signal_t      *sig;
    struct sigaction   sa;

    for (sig = signals; sig->signo != 0; sig++) {
        memset(&sa, 0,sizeof(struct sigaction));

        if (sig->handler) {
            sa.sa_sigaction = sig->handler;
            sa.sa_flags = SA_SIGINFO;
        } else {
            sa.sa_handler = SIG_IGN;
        }
        sigemptyset(&sa.sa_mask);
        if (sigaction(sig->signo, &sa, NULL) == -1) {
            std::cout<<"error init signal "<<sig->name<<std::endl;
        }
    }
    return 0;
}

abc_process_t abc_processes[ABC_MAX_PROCESSES];
int abc_process_slot;
int abc_last_process;
abc_pid_t     abc_pid;
abc_pid_t     abc_parent;
typedef void (*abc_task_fun_t)(void *user_data);
abc_task_fun_t user_task_fun=nullptr;
void abc_worker_process(){
    abc_init_signals();
    abc_process_t *ptr=&abc_processes[abc_process_slot];
    if(user_task_fun){
        user_task_fun((void*)ptr->priv);
    }
}
abc_pid_t abc_spawn_process(const char*priv,int sz){
    abc_pid_t pid;
    if(sz<0||sz>ABC_PRIV_SZ){
        return ABC_INVALID_PID;
    }
    int s=0;
    for(s=0;s<abc_last_process;s++){
        if(-1==abc_processes[s].pid){
            break;
        }
    }
    abc_process_slot=s;
    if(priv&&sz>0){
        memcpy(abc_processes[s].priv,priv,sz);
    }
    pid = fork();
    switch(pid){
        case -1:{
            std::cout<<"fork failed"<<std::endl;
            return ABC_INVALID_PID;
        }
        case 0:{
            abc_parent=abc_pid;
            abc_pid=abc_getpid();
            abc_worker_process();
            break;
        }
        default:
            break;
    }
    if(0==pid){
        exit(0);
    }else if(pid>0){
        abc_processes[s].pid=pid;
        abc_last_process++;
    }
    return pid;
}
void abc_signal_workers(int signo){
    for(int i=0;i<abc_last_process;i++){
        abc_pid_t pid=abc_processes[i].pid;
        if(pid>0){
            if(-1==kill(pid,signo)){
                std::cout<<"kill erorr"<<strerror(abc_errno)<<std::endl;
            }
            int status=0;
            if(-1==waitpid(pid,&status,0)){
                std::cout<<"waitpid erorr"<<strerror(abc_errno)<<" "<<status<<std::endl;
            }
            abc_processes[i].pid=-1;
        }
    }
    abc_last_process=0;    
}
int abc_worker_status(abc_pid_t pid){
    int status;
    int ret=waitpid(pid,&status,WNOHANG);
    if(-1==ret){
        std::cout<<pid<<strerror(abc_errno)<<std::endl;
    }
    return ret;
}
int abc_alive_workers(){
    int counter=abc_last_process;
    int sig_term=abc_signal_value(ABC_TERMINATE_SIGNAL);
    int n=counter;
    for(int i=n-1;i>=0;i--){
        abc_pid_t pid=abc_processes[i].pid;
        int alive=1;
        if(pid>0){
            int status;
            int ret=waitpid(pid,&status,WNOHANG);
            if(pid==ret){
                kill(pid,sig_term);
                abc_processes[i].pid=-1;
                counter--;
                alive=0;
            }
            if(-1==ret){
                if(ECHILD==abc_errno){
                    abc_processes[i].pid=-1;
                    counter--;
                    alive=0;                    
                }
            }
        }else{
            counter--;
            alive=0;
        }
        if(0==alive&&i==abc_last_process-1){
            abc_last_process--;
        }
    }
    return counter;
}
namespace{
    const char *bind_ip=(const char*)"127.0.0.1";
    const uint16_t bind_port=2233;
    const int kMaxBacklog=128;
    const int kMaxEvents=64;
    const int kNumAgents=2;
    const int kEpochs=4;
    const int kBufferSize=1500;
}
using namespace ns3;
using namespace std;
using namespace tcp;
class RequestDispatcher{
public:
    RequestDispatcher(uint32_t num_agent,uint32_t epochs);
    ~RequestDispatcher();
    bool MaybeExit();
    void OnRequest(abc_user_t &request);
private:
    class Agent{
    public:
        ~Agent();
        bool OnRequest(abc_user_t &request);
        bool Consumable(uint32_t current);
        const abc_user_t *front() const;
        void pop_front();
    private:
        uint32_t epoch_=0;
        std::deque<abc_user_t> wait_list_;
    };
    std::vector<Agent> agents_;
    int taken_n_=0;
    uint32_t e_=0;
    uint32_t epochs_;
    bool wait_=false;
};
RequestDispatcher::Agent::~Agent(){
    std::deque<abc_user_t> null_deque;
    null_deque.swap(wait_list_);
}
bool RequestDispatcher::Agent::OnRequest(abc_user_t &request){
    bool taken=false;
    if(request.epoch==epoch_&&wait_list_.empty()){
        wait_list_.push_back(request);
        taken=true;
        return taken;
    }
    if(request.epoch>=epoch_){
        uint32_t largest=epoch_+wait_list_.size();
        if(request.epoch>=largest){
            for(uint32_t i=largest;i<request.epoch;i++){
                abc_user_t add;
                add.uuid=std::numeric_limits<uint32_t>::max();
                wait_list_.push_back(add);
            }
            wait_list_.push_back(request);
            taken=true;
        }else{
            uint32_t index=request.epoch-epoch_;
            if(wait_list_[index].uuid==std::numeric_limits<uint32_t>::max()){
                wait_list_[index]=request;
                taken=true;
            }
        }
    }
    return taken;
}
bool RequestDispatcher::Agent::Consumable(uint32_t current){
    bool ret=false;
    if(wait_list_.size()>0){
        auto ele=wait_list_.front();
        if(ele.uuid!=std::numeric_limits<uint32_t>::max()&&ele.epoch==current){
            ret=true;
        }
    }
    return ret;
}
const abc_user_t *RequestDispatcher::Agent::front() const{
   const abc_user_t * priv=nullptr;
   if(wait_list_.size()>0){
        priv=&wait_list_.front();
   }
   return priv;
}
void RequestDispatcher::Agent::pop_front(){
    if(!wait_list_.empty()){
        wait_list_.pop_front();
        epoch_++;
    }
}
RequestDispatcher::RequestDispatcher(uint32_t num_agent,uint32_t epochs)
:agents_(num_agent),epochs_(epochs){}
RequestDispatcher::~RequestDispatcher(){
    std::vector<Agent> null_vec;
    null_vec.swap(agents_);
}
bool RequestDispatcher::MaybeExit(){
    bool ret=false;
    int num=agents_.size();
    int count=0;
    if(taken_n_>=num&&!wait_){
        for(int i=0;i<num;i++){
            if(agents_[i].Consumable(e_)){
                count++;
            }
        }
        if(count==num){
            int alive_process=0;
            for(int i=0;i<count;i++){
                const abc_user_t *user=agents_[i].front();
                if(abc_spawn_process((const char*)user,sizeof(abc_user_t))>0){
                    alive_process++;
                }
                agents_[i].pop_front();
            }
            if(alive_process!=count){
                std::cout<<"spawn child process failed"<<std::endl;
                int sig_term=abc_signal_value(ABC_TERMINATE_SIGNAL);
                abc_signal_workers(sig_term);
                exit(0);
            }else{
                wait_=true;
            }
            e_++;
            std::cout<<"round "<<e_<<std::endl;
            taken_n_-=num;
        }
    }
    if(0==abc_alive_workers()){
        wait_=false;
    }
    if(e_>=epochs_&&0==abc_alive_workers()){
        ret=true;
    }
    return ret;
}
void RequestDispatcher::OnRequest(abc_user_t &request){
    uint32_t uuid=request.uuid;
    uint32_t epoch=request.epoch;
    if(epoch<epochs_&&uuid>=0&&uuid<agents_.size()){
        if(agents_[uuid].OnRequest(request)){
            taken_n_++;
        }
    }
}
void ParserBuffer(RequestDispatcher *dispatcher,const char *buffer,int sz){
    int msg_size=sizeof(uint16_t)+4*sizeof(uint32_t);
    if(sz<msg_size){
        std::cout<<"oop, sz not right"<<std::endl;
        return ;
    }
    int off=0;
    uint16_t hz=0;
    uint32_t uuid=0;
    uint32_t epoch=0;
    uint32_t start=0;
    uint32_t count=0;
    memcpy((void*)&hz,buffer+off,sizeof(hz));
    off+=sizeof(hz);
    memcpy((void*)&uuid,buffer+off,sizeof(uuid));
    off+=sizeof(uuid);
    memcpy((void*)&epoch,buffer+off,sizeof(epoch));
    off+=sizeof(epoch);
    memcpy((void*)&start,buffer+off,sizeof(start));
    off+=sizeof(start);
    memcpy((void*)&count,buffer+off,sizeof(count));
    off+=sizeof(count);
    assert(off==sz);
    hz=NetToHost16(hz);
    uuid=NetToHost32(uuid);
    epoch=NetToHost32(epoch);
    start=NetToHost32(start);
    count=NetToHost32(count);
    abc_user_t user;
    user.uuid=uuid;
    user.epoch=epoch;
    user.start=start;
    user.count=count;
    dispatcher->OnRequest(user);
}
int main(int argc, char *argv[]){
    uint32_t last=TimeMillis();
    abc_init_signals();
    abc_process_slot=0;
    abc_last_process=0;
    memset((void*)abc_processes,0,sizeof(abc_processes));
    struct epoll_event events[kMaxEvents];
    abc_parent=abc_getppid();
    abc_pid=abc_getpid();
    user_task_fun=ns3_main;
    int listen_fd=create_listen_fd(bind_ip,bind_port,kMaxBacklog);
    int epfd,nfds;
    std::set<int> fd_db;
    char buffer[kBufferSize];
    RequestDispatcher dispatcher(kNumAgents,kEpochs);
    if(listen_fd<0){
        return -1;
    }
    epfd = epoll_create1(0);
    if(-1==epfd){
        close(listen_fd);
        return -1;
    }
    std::cout<<bind_ip<<":"<<bind_port<<std::endl;
    setnonblocking(listen_fd);
    epoll_ctl_add(epfd,listen_fd,EPOLLIN|EPOLLET);
    while(g_running){
        nfds= epoll_wait (epfd, events, kMaxEvents, 0);
        for(int i=0;i<nfds;i++){
            int fd=events[i].data.fd;    
            if((events[i].events & EPOLLERR) ||
              (events[i].events & EPOLLHUP) ||
              (!(events[i].events & EPOLLIN))){
                if(fd!=listen_fd){
                    epoll_ctl_del(epfd,fd);
                    fd_db.erase(fd);
                }
                close (fd);
                continue;
            }
            if(events[i].events &EPOLLIN){
                if (listen_fd==fd){
                    int new_fd=-1;
                    sockaddr_storage addr_storage;
                    socklen_t addr_len = sizeof(addr_storage);
                    while((new_fd=accept(listen_fd,(sockaddr*)&addr_storage,&addr_len))>=0){
                        setnonblocking(new_fd);
                        fd_db.insert(new_fd);
                        epoll_ctl_add(epfd,new_fd,EPOLLIN|EPOLLET);
                    }
                }else{
                    while(true){
                        int off=0;
                        int n=read(fd,buffer+off,kBufferSize-off);
                        if(-1==n){
                            if(EAGAIN==errno){
                                std::cout<<fd<<" read error: "<<strerror(errno)<<std::endl;
                                continue;
                            }else{
                                epoll_ctl_del(epfd,fd);
                                close(fd);
                                fd_db.erase(fd);                            
                                break;
                            }  
                        }else if(0==n){
                            std::cout<<fd<<" conection closed"<<std::endl;
                            epoll_ctl_del(epfd,fd);
                            close(fd);
                            fd_db.erase(fd);
                            break;
                        }else{
                            //since message less than 1500,parse it here
                            //and close sock.
                            off+=n;
                            ParserBuffer(&dispatcher,buffer,off);
                            epoll_ctl_del(epfd,fd);
                            close(fd);
                            fd_db.erase(fd);
                            break;
                        }
                    }
                }
            }
        }
        if(dispatcher.MaybeExit()){
            break;
        }
    }
    
    for(auto it=fd_db.begin();it!=fd_db.end();it++){
        int fd=(*it);
        epoll_ctl_del(epfd,fd);
        close(fd);
    }
    epoll_ctl_del(epfd,listen_fd);
    close(listen_fd);
    close(epfd);
    std::cout<<"last: "<<abc_last_process<<std::endl;
    if(abc_last_process>0){
        int sig_term=abc_signal_value(ABC_TERMINATE_SIGNAL);
        abc_signal_workers(sig_term);
    }

    uint32_t delta=TimeMillis()-last;
    std::cout<<"main pid: "<<abc_pid<<std::endl;
    std::cout<<"run_time:"<<delta<<std::endl;
    return 0;
}
