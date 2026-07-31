#ifndef BUZZER_H
#define BUZZER_H

#define BUZZER_STARTUP 1
#define Buzzer_TRIGGER 2
#define Buzzer_ERROR_START 3
#define Buzzer_INDICATE_OTA 4
#define Buzzer_ERROR_STOP 5

void Buzzer_Task(void *params);

#endif
