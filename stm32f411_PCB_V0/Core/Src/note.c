#include "note.h"
#include <string.h>


typedef struct
{
    const char *name;
    float frequency;

} NoteEntry;


/*
   Accordage standard A4 = 440 Hz

   Do1 = C1
   ...
   Do10 = C10
*/


static const NoteEntry note_table[] =
{

    {"Do1", 32.70f},
    {"Re1", 36.71f},
    {"Mi1", 41.20f},
    {"Fa1", 43.65f},
    {"Sol1",49.00f},
    {"La1", 55.00f},
    {"Si1", 61.74f},


    {"Do2", 65.41f},
    {"Re2", 73.42f},
    {"Mi2", 82.41f},
    {"Fa2", 87.31f},
    {"Sol2",98.00f},
    {"La2",110.00f},
    {"Si2",123.47f},


    {"Do3",130.81f},
    {"Re3",146.83f},
    {"Mi3",164.81f},
    {"Fa3",174.61f},
    {"Sol3",196.00f},
    {"La3",220.00f},
    {"Si3",246.94f},


    {"Do4",261.63f},
    {"Re4",293.66f},
    {"Mi4",329.63f},
    {"Fa4",349.23f},
    {"Sol4",392.00f},
    {"La4",440.00f},
    {"Si4",493.88f},


    {"Do5",523.25f},
    {"Re5",587.33f},
    {"Mi5",659.25f},
    {"Fa5",698.46f},
    {"Sol5",783.99f},
    {"La5",880.00f},
    {"Si5",987.77f},


    {"Do6",1046.50f},
    {"Re6",1174.66f},
    {"Mi6",1318.51f},
    {"Fa6",1396.91f},
    {"Sol6",1567.98f},
    {"La6",1760.00f},
    {"Si6",1975.53f},


    {"Do7",2093.00f},
    {"Re7",2349.32f},
    {"Mi7",2637.02f},
    {"Fa7",2793.83f},
    {"Sol7",3135.96f},
    {"La7",3520.00f},
    {"Si7",3951.07f},


    {"Do8",4186.01f},
    {"Re8",4698.63f},
    {"Mi8",5274.04f},
    {"Fa8",5587.65f},
    {"Sol8",6271.93f},
    {"La8",7040.00f},
    {"Si8",7902.13f},


    {"Do9",8372.02f},
    {"Re9",9397.27f},
    {"Mi9",10548.08f},
    {"Fa9",11175.30f},
    {"Sol9",12543.85f},
    {"La9",14080.00f},
    {"Si9",15804.27f},


    {"Do10",16744.04f},
    {"Re10",18794.55f},
    {"Mi10",21096.16f},
    {"Fa10",22350.60f},
    {"Sol10",25087.70f},
    {"La10",28160.00f},
    {"Si10",31608.53f}

};


#define NOTE_COUNT (sizeof(note_table)/sizeof(NoteEntry))


float Note_GetFrequency(const char *name)
{

    for(int i=0;i<NOTE_COUNT;i++)
    {
        if(strcmp(note_table[i].name,name)==0)
        {
            return note_table[i].frequency;
        }
    }


    // note inconnue
    return 0.0f;
}
