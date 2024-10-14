#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <vector>
#include <mutex>
#include <sys/stat.h>
#include <sys/types.h>
#include <chrono>
#include <unistd.h>
#include <algorithm>
#include <syslog.h>
#include <csignal>

std::mutex mtx;

struct Config {
    std::string sourceDir;
    std::string backupDir;
    std::string logFile;
    int backupFrequency; // in seconds
};

bool is_running = 1;

Config load_config(const std::string filename){
    Config config;
    std::ifstream cFile(filename);
    if (cFile.is_open())
    {
        std::string line;
        while(getline(cFile, line))
        {
            line.erase(std::remove_if(line.begin(), line.end(), isspace), line.end());
            if( line.empty() || line[0] == '#' )
            {
                continue;
            }
            auto delimiterPos = line.find("=");
            auto name = line.substr(0, delimiterPos);
            std::string value;
            if(delimiterPos != std::string::npos)
                value = line.substr(delimiterPos + 1);

            if(name == "source_directory"){
                config.sourceDir = value;
            }
            if(name == "destination_directory")
                config.backupDir = value;
            if(name == "log_file")
                config.logFile = value;
            if(name == "backup_frequency"){
                config.backupFrequency = std::stoi( value );
            }
        }
    }
    else 
    {   
        syslog(LOG_ERR,"Couldn't open config file for reading.\n");
    }

    return config;
}
void backup_file(const std::filesystem::path &src, const std::filesystem::path &dst) {
    openlog("backup_daemon", LOG_PID, LOG_DAEMON);
    std::filesystem::file_time_type first_time = std::filesystem::last_write_time(src);
    if(std::filesystem::exists(dst)){
        std::filesystem::file_time_type second_time = std::filesystem::last_write_time(dst);
        if(first_time != second_time){
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
            std::lock_guard<std::mutex> lock(mtx);
            std::filesystem::last_write_time(dst, first_time);
            
            std::cout << "Backed up: " << src << " to " << dst << std::endl;
            syslog(LOG_INFO, "Backup completed successfully.");
        }
    }else{
        std::filesystem::copy_file(src, dst);
        std::lock_guard<std::mutex> lock(mtx);
        std::filesystem::last_write_time(dst, first_time);
    
        std::cout << "Backed up: " << src << " to " << dst << std::endl;
        syslog(LOG_INFO, "Backup completed successfully.");
    }
}
void backup_directory(const Config& config) {
    if (!std::filesystem::exists(config.backupDir)) {
        std::filesystem::create_directory(config.backupDir);
    }

    std::vector<std::thread> threads;
    for (const auto &entry : std::filesystem::directory_iterator(config.sourceDir)) {
        threads.emplace_back(std::thread(backup_file, entry.path(), config.backupDir + "/" + (std::string)entry.path().filename()));
    }

    for (auto &x : threads) {
        if (x.joinable()) {
            x.join();
        }
    }
}

void pause_handler(int sig_num) {
    is_running = false;
    syslog(LOG_INFO, "Pause");
}

void continue_handler(int sig_num) {
    is_running = true;
    syslog(LOG_INFO, "Continue");
}

void my_terminate_handler(int sig_num) {
    std::cout << "Terminate" << "\n";
    syslog(LOG_INFO, "Terminate");
    closelog(); 
    exit(EXIT_SUCCESS);
}

void status_handler(int sig_num) {
    if (is_running) {
        syslog(LOG_INFO, "Daemon is running.");
    } else {
        syslog(LOG_INFO, "Daemon is paused.");
    }
}
void setup_signal_handlers() {
    signal(SIGTSTP, pause_handler);
    signal(SIGCONT, continue_handler);
    signal(SIGTERM, my_terminate_handler);
    signal(SIGUSR1, status_handler);
}

void loop(){
    openlog("backup_daemon", LOG_PID, LOG_DAEMON);
    

    Config config = load_config("/home/the_greatest_maestro/Documents/it/code/daemon/backup_daemon.conf");

    while(true){
        if(is_running){
            backup_directory(config);
            std::this_thread::sleep_for(std::chrono::seconds(config.backupFrequency));
        }
    }

}
int main(int argc, char *argv[]) {
    openlog("backup_daemon", LOG_PID, LOG_LOCAL0);
    setup_signal_handlers();
    
    std::cout << "Starting daemon." << "\n";
    syslog(LOG_INFO, "Starting daemon.");
    loop();
    closelog();

    return 0;
}
