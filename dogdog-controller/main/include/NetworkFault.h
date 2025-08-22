#ifndef __NETWORK_FAULT_H
#define __NETWORK_FAULT_H

#define NOTHING_ALIVE -1
#define START_ALIVE 0
#define STOP_ALIVE 1

typedef struct StationConnectivityStatus
{
    int station; // 0 = Good Signal; 1 = Bad Signal; 2 = Fault
    int signal;  // 0 = Good Signal; 1 = Bad Signal; 2 = Fault
} StationConnectivityStatus;

void Network_Fault_Task(void *params);
void sendFaultInformation(int start, int stop);

#endif