/*
Author: zsy  
Create: 2021/03/18  
*/
#include <iostream>
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include "ns3/ping-pong-module.h"
#include "ns3/log.h"
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
    }else{
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
            if(pid==abc_worker_status(pid)){
                kill(pid,sig_term);
                abc_processes[i].pid=-1;
                counter--;
                alive=0;
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
using namespace ns3;
int main(int argc, char *argv[]){
    uint32_t last=TimeMillis();
    abc_init_signals();
    abc_process_slot=0;
    abc_last_process=0;
    memset((void*)abc_processes,0,sizeof(abc_processes));
    abc_parent=abc_getppid();
    abc_pid=abc_getpid();
    int n=2;
    int alive=0;
    abc_user_t user[2];
    memset(user,0,2*sizeof(abc_user_t));
    user[0].start=0;
    user[0].count=20;
    user[1].start=30;
    user[1].count=10;
    user_task_fun=ns3_main;
    for(int i=0;i<n;i++){
        if(abc_spawn_process((const char*)&user[i],sizeof(abc_user_t))>0){
            alive++;
        }
    }
    for(int i=0;i<ABC_MAX_PROCESSES;i++){
        abc_pid_t pid=abc_processes[i].pid;
        if(pid>0){
            int ret=abc_worker_status(pid);
            std::cout<<"check "<<pid<<" "<<ret<<std::endl;
        }
    }
    while(g_running){
        if(0==abc_alive_workers()){
            break;
        }
    }
    for(int i=0;i<abc_last_process;i++){
        abc_pid_t pid=abc_processes[i].pid;
        if(pid>0){
            int ret=abc_worker_status(pid);
            std::cout<<"check "<<pid<<" "<<ret<<std::endl;
        }
    }
    uint32_t delta=TimeMillis()-last;
    std::cout<<"run_time:"<<delta<<std::endl;
    return 0;
}
