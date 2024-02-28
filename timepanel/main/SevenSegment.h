void Seven_Segment_Task(void *params);

void setupSevenSegment();
void shiftSevenSegmentNumber(int a, int b, int c, int d, int e, int f, int g, int dot);
void setSevenSegment();
void setZero(int dot);
void setOne(int dot);
void setTwo(int dot);
void setThree(int dot);
void setFour(int dot);
void setFive(int dot);
void setSix(int dot);
void setSeven(int dot);
void setEight(int dot);
void setNine(int dot);
void setNumber(int i, int dot);
void clearSevenSegment();
void setMilliseconds(long timeToSet);
void displayFault(int start, int stop, int dots);

typedef struct SevenSegmentDisplay
{
    int time;
    int type;
    int startFault; // 1 = Fault
    int stopFault;  // 1 = Fault
} SevenSegmentDisplay;

#define SEVEN_SEGMENT_SET_TIME 0
#define SEVEN_SEGMENT_NETWORK_FAULT 1