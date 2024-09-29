/*
	Author of the starter code
    Yifan Ren
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 9/15/2024
	
	Please include your Name, UIN, and the date below
	Name: Jaya Sundarrajan
	UIN: 803003069
	Date: 9/20/2024
*/


#include "common.h"
#include "FIFORequestChannel.h"
#include <sys/wait.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h> 


using namespace std;

int main(int argc, char *argv[]) {
    int opt;
    int p = 1;  
    double t = 0.0;  
    int e = 1;  
    string filename = "";  


    int chunk_size = MAX_MESSAGE;
    bool new_channel_requested = false;
    bool is_single_request = false;



  
    while ((opt = getopt(argc, argv, "p:t:e:f:m:c")) != -1) {
        switch (opt) {
            case 'p':
                p = atoi(optarg);  
                break;
            case 't':
                t = atof(optarg);  
                is_single_request = true;
                break;
            case 'e':
                e = atoi(optarg);  
                break;
            case 'f':
                filename = optarg;  
                break;
            case 'm':
                chunk_size = atoi(optarg);  // Buffer size for file transfer
                break;
            case 'c':
                new_channel_requested = true;  // Request a new channel
                break;
            default:
                cerr << "Usage: " << argv[0] << " [-p patient_id] [-t time] [-e ecg_type] "
                     << "[-f filename] [-m chunk_size] [-c]" << endl;
                exit(EXIT_FAILURE);
        }
    }



    //Task 1:
	//Run the server process as a child of the client process


    pid_t pid = fork();
    if (pid == 0) {
        // Child process: run the server
        if (setsid() == -1) {
            perror("setsid failed");
            exit(EXIT_FAILURE);
        }
        cout << "Starting the server..." << std::endl;
        string chunk_size_str = to_string(chunk_size); 
        char *args_exec[] = {const_cast<char*>("./server"), const_cast<char*>("-m"),
                             const_cast<char*>(chunk_size_str.c_str()), nullptr};
        execvp(args_exec[0], args_exec);
        perror("Error starting server process");
        exit(1);
    } else if (pid < 0) {
        perror("Fork failed");
        exit(1);
    }

    // waiting .2 sec for server to ready
    usleep(200000); 

    // FIFORequestChannel chan("control", FIFORequestChannel::CLIENT_SIDE);

    FIFORequestChannel *control_chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE); // create a control channel
    FIFORequestChannel *new_data_chan = control_chan;  //default


    //Task 4:
	//Request a new channel


    if (new_channel_requested) {
        cout << "requesting a new channel..." << endl;
        MESSAGE_TYPE new_channel_request = NEWCHANNEL_MSG;
        control_chan->cwrite(&new_channel_request, sizeof(MESSAGE_TYPE));

        char new_channel_name[100];
        memset(new_channel_name, 0, sizeof(new_channel_name));  
        control_chan->cread(new_channel_name, sizeof(new_channel_name));
        cout << "created new chanel: " << new_channel_name << endl;

        new_data_chan = new FIFORequestChannel(new_channel_name, FIFORequestChannel::CLIENT_SIDE);
    }

    
    const string received_dir = "received/"; // makes sure recieved  exists
    struct stat st;
    if (stat(received_dir.c_str(), &st) == -1) {
        if (mkdir(received_dir.c_str(), 0700) == -1) {
            perror("Failed to create 'received' directory");
            delete new_data_chan;
            kill(pid, SIGKILL);
            exit(EXIT_FAILURE);
        }
    }


   //Task 2:
	//Request data points
    

    if (filename.empty()) {
        if (is_single_request) {
            // request single ECG data point
            datamsg x(p, t, e);
            new_data_chan->cwrite(&x, sizeof(datamsg));

            double reply;
            new_data_chan->cread(&reply, sizeof(double));
	        cout << "For person " << p << ", at time " << t << ", the value of ecg " << e << " is " << reply << endl;
        } else {
            const int num_points = 1000;  
            const double dt = 0.004;      
            double current_time = 0.0;

            
            // string ecg_filename = to_string(p) + ".csv";
            // if (p == 9) {
            //     ecg_filename = "x1.csv";
            // }

            string ecg_filename = "x1.csv";

           

            ofstream outFile(received_dir + ecg_filename);
            if (!outFile.is_open()) {
                cerr << "Error opening file: " << received_dir + ecg_filename << endl;
                delete new_data_chan;
                kill(pid, SIGKILL);
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < num_points; ++i) {
                

                // gete ecg 1 
                datamsg x_ecg1(p, current_time, 1);
                new_data_chan->cwrite(&x_ecg1, sizeof(datamsg));

                double ecg1_value;
                new_data_chan->cread(&ecg1_value, sizeof(double));

                // get ecg 2
                datamsg x_ecg2(p, current_time, 2);
                new_data_chan->cwrite(&x_ecg2, sizeof(datamsg));

                double ecg2_value;
                new_data_chan->cread(&ecg2_value, sizeof(double));

                
                outFile << current_time << "," << ecg1_value << "," << ecg2_value << endl;

                
                current_time += dt;
            }
            outFile.close();
            cout << "ecg data saved to:" << received_dir + ecg_filename << endl;
        }
    }

    // Task 3:
    //Request files
    if (!filename.empty()) {
        cout << "Requesting file: " << filename << endl;

        filemsg fm(0, 0);
        int len = sizeof(filemsg) + filename.size() + 1;
        char *buf = new char[len];
        memcpy(buf, &fm, sizeof(filemsg));
        strcpy(buf + sizeof(filemsg), filename.c_str());

        new_data_chan->cwrite(buf, len);

        __int64_t file_length;
        new_data_chan->cread(&file_length, sizeof(__int64_t));
        cout << "File size: " << file_length << " bytes" << endl;


        string transfer_file_name = to_string(p) + ".csv";

        // open recieved to write

        FILE *fp = fopen((received_dir + transfer_file_name).c_str(), "wb");
        if (!fp) {
            cerr << "Error opening file: " << filename << endl;
            delete[] buf;
            delete new_data_chan;
            kill(pid, SIGKILL);
            exit(EXIT_FAILURE);
        }

        // read chunks of file




        __int64_t remaining = file_length;
        __int64_t offset = 0;
        while (remaining > 0) {
            __int64_t request_size = min((__int64_t)chunk_size, remaining);

            filemsg fm_chunk(offset, request_size);
            len = sizeof(filemsg) + filename.size() + 1;
            char *buf_chunk = new char[len];
            memcpy(buf_chunk, &fm_chunk, sizeof(filemsg));
            strcpy(buf_chunk + sizeof(filemsg), filename.c_str());

            new_data_chan->cwrite(buf_chunk, len);

            char *file_buf = new char[request_size];
            new_data_chan->cread(file_buf, request_size);
            fwrite(file_buf, 1, request_size, fp);


            // freee memory
            delete[] file_buf;
            delete[] buf_chunk;  
            offset += request_size;
            remaining -= request_size;
        }

        fclose(fp);
        cout << "file transfer complete: " << filename << endl;
        delete[] buf;
    }

    // Task 5:
    // Closing all the channels
    MESSAGE_TYPE quit_msg = QUIT_MSG;
    

    
    if (new_channel_requested) {
        control_chan->cwrite(&quit_msg, sizeof(MESSAGE_TYPE));
        new_data_chan->cwrite(&quit_msg, sizeof(MESSAGE_TYPE));
    } else {
        new_data_chan->cwrite(&quit_msg, sizeof(MESSAGE_TYPE));
    }

    wait(NULL);

    // clean up channels
    if (new_channel_requested) {
        delete new_data_chan;      
        delete control_chan;   
    } else {
        delete new_data_chan;     
    }

    return 0;
}
