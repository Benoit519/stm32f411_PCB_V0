#ifndef NOTES_H
#define NOTES_H

#include <stdint.h>


typedef struct
{
    const char *name;
    float frequency;

} NoteFreq;



static const NoteFreq note_table[] =
{

    {"Do1", 32.70f},
    {"Re1", 36.71f},
    {"Mi1", 41.20f},
    {"Fa1", 43.65f},
    {"Sol1",49.00f},
    {"La1",55.00f},
    {"Si1",61.74f},


    {"Do2",65.41f},
    {"Re2",73.42f},
    {"Mi2",82.41f},
    {"Fa2",87.31f},
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
    {"Si4",493.88f}

};


#define NOTE_TABLE_SIZE \
(sizeof(note_table)/sizeof(NoteFreq))


static float Note_GetFrequency(const char *name)
{

    for(int i=0;i<NOTE_TABLE_SIZE;i++)
    {

        if(strcmp(name,note_table[i].name)==0)
        {
            return note_table[i].frequency;
        }

    }


    return 0.0f;

}


#endif
