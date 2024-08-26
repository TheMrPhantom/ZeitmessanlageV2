#define NOTHING_ALIVE -1
#define START_ALIVE 0
#define STOP_ALIVE 1

void Network_Fault_Task(void *params);
void sendFaultInformation(bool start, bool stop);