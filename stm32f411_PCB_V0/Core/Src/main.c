/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "mcp23017.h"
#include <math.h>
#include "wavetable.h"
#include <string.h>
#include "note.h"

#define ATTACK_TIME_MS   20.0f
#define RELEASE_TIME_MS 120.0f

#define SUSTAIN_LEVEL 0.8f
#define AMPLITUDE 28000.0f

#define MAX_VOICES 16

typedef enum
{
    MODE_PUSH,
    MODE_PULL

} BellowsMode;


volatile BellowsMode bellows_mode = MODE_PUSH;
typedef enum
{
    HAND_LEFT,
    HAND_RIGHT

} Hand;

typedef enum
{
    ENV_OFF,
    ENV_ATTACK,
    ENV_SUSTAIN,
    ENV_RELEASE

} EnvelopeState;

typedef struct
{
    uint8_t active;

    const char *note;

    Hand hand;

    float frequency;

    /* Accumulateur DDS 32 bits :
       bits [31:23] = index wavetable (0-511)   WAVETABLE_SIZE = 512 = 2^9
       Wrap naturel par debordement uint32_t, pas de modulo. */
    uint32_t phase_acc;
    uint32_t phase_inc_nom;


    /*
       enveloppe ADSR
    */

    EnvelopeState env_state;

    float env_level;

    float attack_step;
    float release_step;


    float sustain_level;


    float amplitude;


    const int16_t *wave;

} Voice;

static Voice voices[MAX_VOICES];
typedef enum
{
    WT_ACCORDION,
    WT_ORGAN,
    WT_FLUTE,
    WT_STRINGS
} WaveTableId;


static void Voice_SetWave(Voice *v, WaveTableId wt);

#define BUFFER_SIZE 256
#define HALF_BUFFER_SIZE (BUFFER_SIZE / 2)

static int16_t bufferDMA[BUFFER_SIZE];
static const float SAMPLE_RATE = 44100.0f;
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

I2S_HandleTypeDef hi2s1;
DMA_HandleTypeDef hdma_spi1_tx;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2S1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* -------------------------------------------------------------------------- */
/* Hardware / UI logic: periodic scan (from previous optimized version)         */
/* -------------------------------------------------------------------------- */
static uint8_t mcp_state[4][2];
volatile uint16_t pressure = 0;

MCP23017_HandleTypeDef hmcp20;
MCP23017_HandleTypeDef hmcp21;
MCP23017_HandleTypeDef hmcp22;
MCP23017_HandleTypeDef hmcp23;

typedef struct
{
    const char *notes[2];
    WaveTableId wavetable;
} ButtonSound;

typedef struct
{
    uint8_t mcp;
    uint8_t port;
    uint8_t bit;
    Hand hand;
    ButtonSound push;
    ButtonSound pull;
} Button;
Button buttons[] =
{

/******** MCP20 - main gauche ********/

{20,0,0, HAND_LEFT, {{"Re3",  NULL}, WT_ACCORDION}, {{"Fad3",  NULL}, WT_ACCORDION}},
{20,0,1, HAND_LEFT, {{"Mi3",  NULL}, WT_ACCORDION}, {{"Lad3",  NULL}, WT_ACCORDION}},
{20,0,2, HAND_LEFT, {{"Sol3",  NULL}, WT_ACCORDION}, {{"La3",  NULL}, WT_ACCORDION}},
{20,0,3, HAND_LEFT, {{"Sold3",  NULL}, WT_ACCORDION}, {{"Si3",  NULL}, WT_ACCORDION}},
{20,0,4, HAND_LEFT, {{"La3", NULL}, WT_ACCORDION}, {{"Dod4", NULL}, WT_ACCORDION}},
{20,0,5, HAND_LEFT, {{"Si3",  NULL}, WT_ACCORDION}, {{"Do4",  NULL}, WT_ACCORDION}},
{20,0,6, HAND_LEFT, {{"Do4",  NULL}, WT_ACCORDION}, {{"Re4",  NULL}, WT_ACCORDION}},
{20,0,7, HAND_LEFT, {{"Dod4",  NULL}, WT_ACCORDION}, {{"Red4",  NULL}, WT_ACCORDION}},

{20,1,0, HAND_LEFT, {{"Lad4",  NULL}, WT_ACCORDION}, {{"Red4",  NULL}, WT_ACCORDION}},
{20,1,1, HAND_LEFT, {{"Re4",  NULL}, WT_ACCORDION}, {{"Mi4",  NULL}, WT_ACCORDION}},
{20,1,2, HAND_LEFT, {{"Mi4",  NULL}, WT_ACCORDION}, {{"Fa4",  NULL}, WT_ACCORDION}},
{20,1,3, HAND_LEFT, {{"Fa4", NULL}, WT_ACCORDION}, {{"Sol4", NULL}, WT_ACCORDION}},
{20,1,4, HAND_LEFT, {{"Fad4",  NULL}, WT_ACCORDION}, {{"La4",  NULL}, WT_ACCORDION}},
{20,1,5, HAND_LEFT, {{"Sol4",  NULL}, WT_ACCORDION}, {{"Fad4",  NULL}, WT_ACCORDION}},
{20,1,6, HAND_LEFT, {{"Sold4",  NULL}, WT_ACCORDION}, {{"La4",  NULL}, WT_ACCORDION}},
{20,1,7, HAND_LEFT, {{"La4",  NULL}, WT_ACCORDION}, {{"Lad4",  NULL}, WT_ACCORDION}},

/******** MCP21 - main droite ********/

{21,0,0, HAND_RIGHT, {{"Lad4",  NULL}, WT_ACCORDION}, {{"Dod5",  NULL}, WT_ACCORDION}},
{21,0,1, HAND_RIGHT, {{"Si4",  NULL}, WT_ACCORDION}, {{"La4",  NULL}, WT_ACCORDION}},
{21,0,2, HAND_RIGHT, {{"Do5",  NULL}, WT_ACCORDION}, {{"Si4",  NULL}, WT_ACCORDION}},
{21,0,3, HAND_RIGHT, {{"Dod5",  NULL}, WT_ACCORDION}, {{"Re5",  NULL}, WT_ACCORDION}},
{21,0,4, HAND_RIGHT, {{"Red5", NULL}, WT_ACCORDION}, {{"Mi5", NULL}, WT_ACCORDION}},
{21,0,5, HAND_RIGHT, {{"Re5",  NULL}, WT_ACCORDION}, {{"Do5",  NULL}, WT_ACCORDION}},
{21,0,6, HAND_RIGHT, {{"Mi5",  NULL}, WT_ACCORDION}, {{"Re5",  NULL}, WT_ACCORDION}},
{21,0,7, HAND_RIGHT, {{"Fa5",  NULL}, WT_ACCORDION}, {{"Red5",  NULL}, WT_ACCORDION}},

{21,1,0, HAND_RIGHT, {{"Fad5",  NULL}, WT_ACCORDION}, {{"Sold5",  NULL}, WT_ACCORDION}},
{21,1,1, HAND_RIGHT, {{"Sol5",  NULL}, WT_ACCORDION}, {{"Mi5",  NULL}, WT_ACCORDION}},
{21,1,2, HAND_RIGHT, {{"Sold5",  NULL}, WT_ACCORDION}, {{"Fa5",  NULL}, WT_ACCORDION}},
{21,1,3, HAND_RIGHT, {{"La5", NULL}, WT_ACCORDION}, {{"Sol5", NULL}, WT_ACCORDION}},
{21,1,4, HAND_RIGHT, {{"Lad5",  NULL}, WT_ACCORDION}, {{"La5",  NULL}, WT_ACCORDION}},
{21,1,5, HAND_RIGHT, {{"Si5",  NULL}, WT_ACCORDION}, {{"Fad5",  NULL}, WT_ACCORDION}},
{21,1,6, HAND_RIGHT, {{"Do6",  NULL}, WT_ACCORDION}, {{"La5",  NULL}, WT_ACCORDION}},
{21,1,7, HAND_RIGHT, {{"Dod6",  NULL}, WT_ACCORDION}, {{"Lad5",  NULL}, WT_ACCORDION}},

/******** MCP22 ********/

{22,0,0, HAND_RIGHT, {{"Red6",  NULL}, WT_ACCORDION}, {{"Dod6",  NULL}, WT_ACCORDION}},
{22,0,1, HAND_RIGHT, {{"Re6",  NULL}, WT_ACCORDION}, {{"La5",  NULL}, WT_ACCORDION}},
{22,0,2, HAND_RIGHT, {{"Mi6", NULL}, WT_ACCORDION}, {{"Si5", NULL}, WT_ACCORDION}},
{22,0,3, HAND_RIGHT, {{"Fa6",  NULL}, WT_ACCORDION}, {{"Re6",  NULL}, WT_ACCORDION}},
{22,0,4, HAND_RIGHT, {{"Fad6",  NULL}, WT_ACCORDION}, {{"Mi6",  NULL}, WT_ACCORDION}},
{22,0,5, HAND_RIGHT, {{"Sol6",  NULL}, WT_ACCORDION}, {{"Do6",  NULL}, WT_ACCORDION}},
{22,0,6, HAND_RIGHT, {{"Sold6",  NULL}, WT_ACCORDION}, {{"Re6",  NULL}, WT_ACCORDION}},
{22,0,7, HAND_RIGHT, {{"La6",  NULL}, WT_ACCORDION}, {{"Red6",  NULL}, WT_ACCORDION}},

{22,1,0, HAND_RIGHT, {{"Lad6",  NULL}, WT_ACCORDION}, {{"Sold6",  NULL}, WT_ACCORDION}},
{22,1,1, HAND_RIGHT, {{"Do6", NULL}, WT_ACCORDION}, {{"Fa6", NULL}, WT_ACCORDION}},
{22,1,2, HAND_RIGHT, {{"Dod6",  NULL}, WT_ACCORDION}, {{"Sol6",  NULL}, WT_ACCORDION}},
{22,1,3, HAND_RIGHT, {{"Si7",  NULL}, WT_ACCORDION}, {{"Si7",  NULL}, WT_ACCORDION}},
{22,1,4, HAND_RIGHT, {{"Do8",  NULL}, WT_ACCORDION}, {{"Do8",  NULL}, WT_ACCORDION}},
{22,1,5, HAND_RIGHT, {{"Re8",  NULL}, WT_ACCORDION}, {{"Re8",  NULL}, WT_ACCORDION}},
{22,1,6, HAND_RIGHT, {{"Mi8",  NULL}, WT_ACCORDION}, {{"Mi8",  NULL}, WT_ACCORDION}},
{22,1,7, HAND_RIGHT, {{"Fa8",  NULL}, WT_ACCORDION}, {{"Fa8",  NULL}, WT_ACCORDION}},

/******** MCP23 ********/

{23,0,0, HAND_RIGHT, {{"Sol8",  NULL}, WT_ACCORDION}, {{"Sol8",  NULL}, WT_ACCORDION}},
{23,0,1, HAND_RIGHT, {{"La8",   NULL}, WT_ACCORDION}, {{"La8",   NULL}, WT_ACCORDION}},
{23,0,2, HAND_RIGHT, {{"Si8",   NULL}, WT_ACCORDION}, {{"Si8",   NULL}, WT_ACCORDION}},
{23,0,3, HAND_RIGHT, {{"Do9",   NULL}, WT_ACCORDION}, {{"Do9",   NULL}, WT_ACCORDION}},
{23,0,4, HAND_RIGHT, {{"Re9",   NULL}, WT_ACCORDION}, {{"Re9",   NULL}, WT_ACCORDION}},
{23,0,5, HAND_RIGHT, {{"Mi9",   NULL}, WT_ACCORDION}, {{"Mi9",   NULL}, WT_ACCORDION}},
{23,0,6, HAND_RIGHT, {{"Fa9",   NULL}, WT_ACCORDION}, {{"Fa9",   NULL}, WT_ACCORDION}},

{23,1,0, HAND_RIGHT, {{"La9",   NULL}, WT_ACCORDION}, {{"La9",   NULL}, WT_ACCORDION}},
{23,1,1, HAND_RIGHT, {{"Si9",   NULL}, WT_ACCORDION}, {{"Si9",   NULL}, WT_ACCORDION}},
{23,1,2, HAND_RIGHT, {{"Do10",  NULL}, WT_ACCORDION}, {{"Do10",  NULL}, WT_ACCORDION}},
{23,1,3, HAND_RIGHT, {{"Re10",  NULL}, WT_ACCORDION}, {{"Re10",  NULL}, WT_ACCORDION}},
{23,1,4, HAND_RIGHT, {{"Mi10",  NULL}, WT_ACCORDION}, {{"Mi10",  NULL}, WT_ACCORDION}},
{23,1,5, HAND_RIGHT, {{"Fa10",  NULL}, WT_ACCORDION}, {{"Fa10",  NULL}, WT_ACCORDION}},
{23,1,6, HAND_RIGHT, {{"Sol10", NULL}, WT_ACCORDION}, {{"Sol10", NULL}, WT_ACCORDION}},
{23,1,7, HAND_RIGHT, {{"La10",  NULL}, WT_ACCORDION}, {{"La10",  NULL}, WT_ACCORDION}}

};
enum
{
    NB_BUTTONS = sizeof(buttons) / sizeof(buttons[0])
};

static ButtonSound *active_sound[NB_BUTTONS];
static uint8_t previous_buttons[NB_BUTTONS];



static void Update_Bellows_Mode(void)
{
    /*
       MCP23
       GPA7 = bit 7 du port A

       0 = PUSH
       1 = PULL
    */

    uint8_t gpa7 =
        (mcp_state[3][0] >> 7) & 1;


    if(gpa7)
    {
        bellows_mode = MODE_PULL;
    }
    else
    {
        bellows_mode = MODE_PUSH;
    }
}

void Synth_Init(void)
{
    for(int i=0;i<MAX_VOICES;i++)
    {
        voices[i].active = 0;
    }

    for(int i = 0; i < NB_BUTTONS; i++)
    {
        previous_buttons[i] = 0;
        active_sound[i] = NULL;
    }
}

void NoteOn(const char *note,
            Hand hand,
            WaveTableId wavetable)
{
    /* eviter les doublons ; retrigger si la voix est en RELEASE */
    for(int i = 0; i < MAX_VOICES; i++)
    {
        if(voices[i].active &&
           strcmp(voices[i].note, note) == 0 &&
           voices[i].hand == hand)
        {
            if(voices[i].env_state == ENV_RELEASE)
            {
                /* retrigger : reprendre l'attaque depuis le niveau actuel */
                voices[i].env_state = ENV_ATTACK;
            }
            return;
        }
    }

    /* chercher une voix libre */
    int slot = -1;
    for(int i = 0; i < MAX_VOICES; i++)
    {
        if(!voices[i].active)
        {
            slot = i;
            break;
        }
    }

    /* voice stealing : priorité aux voix en RELEASE */
    if(slot == -1)
    {
        for(int i = 0; i < MAX_VOICES; i++)
        {
            if(voices[i].env_state == ENV_RELEASE)
            {
                slot = i;
                break;
            }
        }
    }

    /* fallback : voix 0 (la plus ancienne) */
    if(slot == -1)
    {
        slot = 0;
    }

    /* sécurité note inconnue */
    float freq = Note_GetFrequency(note);
    if(freq <= 0.0f)
    {
        return;
    }

    /* Desactivation d'abord : si voice stealing, l'ISR DMA verra active=0
       et sautera cette voix pendant toute la reinitialisation. */
    voices[slot].active = 0;

    /* Initialisation complete avant reactivation */

    voices[slot].note      = note;
    voices[slot].hand      = hand;
    voices[slot].frequency = freq;

    /* Oscillateur principal DDS 32 bits */

    voices[slot].phase_acc = 0;

    /* increment = f * 2^32 / fs  (calcul flottant a la note-on, pas dans la boucle audio) */
    voices[slot].phase_inc_nom =
        (uint32_t)(freq * 4294967296.0f / SAMPLE_RATE);

    /* Enveloppe : attaque directement jusqu'au sustain (pas de discontinuité 1.0→0.8) */

    voices[slot].env_state     = ENV_ATTACK;
    voices[slot].env_level     = 0.0f;
    voices[slot].sustain_level = SUSTAIN_LEVEL;

    voices[slot].attack_step =
        SUSTAIN_LEVEL /
        ((ATTACK_TIME_MS * SAMPLE_RATE) / 1000.0f);

    /* SUSTAIN_LEVEL / duree : la release part du niveau sustain -> 0 en RELEASE_TIME_MS */
    voices[slot].release_step =
        SUSTAIN_LEVEL /
        ((RELEASE_TIME_MS * SAMPLE_RATE) / 1000.0f);

    Voice_SetWave(&voices[slot], wavetable);

    voices[slot].amplitude = 1.0f;

    /* Activation en dernier : la voix est prête avant d'être visible par l'ISR audio */
    voices[slot].active = 1;
}

void NoteOff(const char *note,
             Hand hand,
             WaveTableId wavetable)
{
    (void)wavetable;   // paramètre inutilisé

    for(int i = 0; i < MAX_VOICES; i++)
    {
        if(voices[i].active &&
           strcmp(voices[i].note, note) == 0 &&
           voices[i].hand == hand)
        {
        	voices[i].env_state = ENV_RELEASE;
        }
    }
}

static float Envelope_Update(Voice *v)
{

    switch(v->env_state)
    {

    case ENV_ATTACK:

        v->env_level += v->attack_step;

        if(v->env_level >= v->sustain_level)
        {
            v->env_level = v->sustain_level;
            v->env_state = ENV_SUSTAIN;
        }

        break;



    case ENV_SUSTAIN:

        v->env_level = v->sustain_level;

        break;



    case ENV_RELEASE:

        v->env_level -= v->release_step;


        if(v->env_level <= 0.0f)
        {
            v->env_level = 0.0f;
            v->env_state = ENV_OFF;
            v->active = 0;
        }

        break;



    case ENV_OFF:

        break;
    }


    return v->env_level;
}

static void MCP_Read_All(void)
{
    mcp_state[0][0] =
        mcp23017_read_gpio_int(&hmcp20, MCP23017_PORTA);

    mcp_state[0][1] =
        mcp23017_read_gpio_int(&hmcp20, MCP23017_PORTB);



    mcp_state[1][0] =
        mcp23017_read_gpio_int(&hmcp21, MCP23017_PORTA);

    mcp_state[1][1] =
        mcp23017_read_gpio_int(&hmcp21, MCP23017_PORTB);



    mcp_state[2][0] =
        mcp23017_read_gpio_int(&hmcp22, MCP23017_PORTA);

    mcp_state[2][1] =
        mcp23017_read_gpio_int(&hmcp22, MCP23017_PORTB);



    mcp_state[3][0] =
        mcp23017_read_gpio_int(&hmcp23, MCP23017_PORTA);

    mcp_state[3][1] =
        mcp23017_read_gpio_int(&hmcp23, MCP23017_PORTB);
}

static uint8_t MCP_Index(uint8_t mcp)
{
    switch(mcp)
    {
        case 20: return 0;
        case 21: return 1;
        case 22: return 2;
        case 23: return 3;
    }

    return 0;
}
/* Renvoie 1 si un autre bouton encore presse tient deja la meme note+main.
   Evite de couper une note partagee entre deux boutons lors d'un NoteOff partiel. */
static uint8_t IsNoteHeldByOtherButton(int except_i, const char *note, Hand hand)
{
    for(int j = 0; j < NB_BUTTONS; j++)
    {
        if(j == except_i) continue;
        if(previous_buttons[j] && active_sound[j] != NULL)
        {
            for(int n = 0; n < 2; n++)
            {
                if(active_sound[j]->notes[n] != NULL &&
                   strcmp(active_sound[j]->notes[n], note) == 0 &&
                   buttons[j].hand == hand)
                {
                    return 1;
                }
            }
        }
    }
    return 0;
}
static void UI_ScanAndDispatch(void)
{
    MCP_Read_All();

    // Lecture du sens du soufflet via MCP23 GPA7
    Update_Bellows_Mode();


    for(int i = 0; i < NB_BUTTONS; i++)
    {
        uint8_t mcp_index = MCP_Index(buttons[i].mcp);

        uint8_t port = buttons[i].port;
        uint8_t bit  = buttons[i].bit;


        uint8_t state =
            (mcp_state[mcp_index][port] >> bit) & 1;

        state = !state;
     
        /*
            APPUI
        */

        if(state && !previous_buttons[i])
        {
            /*
                On capture le son correspondant
                au sens du soufflet AU MOMENT de l'appui
            */

            if(bellows_mode == MODE_PUSH)
            {
                active_sound[i] = &buttons[i].push;
            }
            else
            {
                active_sound[i] = &buttons[i].pull;
            }


            ButtonSound *sound = active_sound[i];


            for(int n = 0; n < 2; n++)
            {
                if(sound->notes[n] != NULL)
                {
                    NoteOn(
                        sound->notes[n],
                        buttons[i].hand,
                        sound->wavetable
                    );
                }
            }
        }



        /*
            RELACHEMENT
        */

        else if(!state && previous_buttons[i])
        {

            ButtonSound *sound = active_sound[i];


            if(sound != NULL)
            {
                for(int n = 0; n < 2; n++)
                {
                    if(sound->notes[n] != NULL &&
                       !IsNoteHeldByOtherButton(i, sound->notes[n], buttons[i].hand))
                    {
                        NoteOff(
                            sound->notes[n],
                            buttons[i].hand,
                            sound->wavetable
                        );
                    }
                }
            }


            active_sound[i] = NULL;
        }


        previous_buttons[i] = state;
    }
}
/* Selectionne la position musicale (wave_index) d'apres la frequence.
   Points de reference : do3=130.81  sol3=196  do4=261.63  sol4=392  do5=523.25  sol5=783.99
   Seuils = moyennes geometriques entre positions adjacentes. */
static int get_wave_index(float frequency)
{
    if(frequency < 160.0f)  return 0;   /* do3  */
    if(frequency < 226.0f)  return 1;   /* sol3 */
    if(frequency < 320.0f)  return 2;   /* do4  */
    if(frequency < 453.0f)  return 3;   /* sol4 */
    if(frequency < 640.0f)  return 4;   /* do5  */
    return 5;                            /* sol5 */
}

/* Selectionne le niveau band-limited (bl_index) d'apres la frequence de lecture.
   Plus la frequence est haute, moins d'harmoniques sont conservees. */
static int get_bl_index(float frequency)
{
    if(frequency < 500.0f)  return 0;   /* BL0 : plein spectre */
    if(frequency < 2000.0f) return 1;   /* BL1 : reduit        */
    if(frequency < 8000.0f) return 2;   /* BL2 : tres reduit   */
    return 3;                            /* BL3 : quasi-sinus   */
}

static void Voice_SetWave(Voice *v, WaveTableId wt)
{
    switch(wt)
    {
    case WT_ACCORDION:
    {
        int wi = get_wave_index(v->frequency);
        int bi = get_bl_index(v->frequency);
        v->wave = wavetable_accordion[wi][bi];
        break;
    }
    default:
        v->wave = wavetable_accordion[0][0];
        break;
    }
}
void render_audio_block(int16_t *buffer,
                        uint32_t samples)
{
    float gain = pressure / 4095.0f;

    for(uint32_t i = 0; i < samples; i++)
    {
        float sample = 0.0f;

        for(int v = 0; v < MAX_VOICES; v++)
        {
            if(voices[v].active)
            {
                /* Lecture wavetable DDS, index seul (pas d'interpolation) : bits [31:23] = 0-511 */
                uint16_t index = (uint16_t)(voices[v].phase_acc >> 23);

                float envelope =
                    Envelope_Update(&voices[v]);

                sample +=
                    (voices[v].wave[index] / 32768.0f) *
                    voices[v].amplitude *
                    envelope;

                voices[v].phase_acc += voices[v].phase_inc_nom;
            }
        }

        float output = sample * gain * AMPLITUDE;

        if(output > 32767.0f)  output = 32767.0f;
        if(output < -32768.0f) output = -32768.0f;

        buffer[i] = (int16_t)output;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1)
    {
        pressure = HAL_ADC_GetValue(hadc);
    }
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    (void)hi2s;
if ((HAL_ADC_GetState(&hadc1) & HAL_ADC_STATE_REG_BUSY) == 0)
{
    HAL_ADC_Start_IT(&hadc1);
}

    render_audio_block((int16_t *)&bufferDMA[0], HALF_BUFFER_SIZE);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    (void)hi2s;
if ((HAL_ADC_GetState(&hadc1) & HAL_ADC_STATE_REG_BUSY) == 0)
{
    HAL_ADC_Start_IT(&hadc1);
}

    render_audio_block((int16_t *)&bufferDMA[HALF_BUFFER_SIZE], HALF_BUFFER_SIZE);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2S1_Init();
  MX_I2C1_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
    mcp23017_init(&hmcp20, &hi2c1, MCP23017_ADDRESS_20);
    mcp23017_iodir(&hmcp20, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(&hmcp20, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);

    mcp23017_init(&hmcp21, &hi2c1, MCP23017_ADDRESS_21);
    mcp23017_iodir(&hmcp21, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(&hmcp21, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);

    mcp23017_init(&hmcp22, &hi2c1, MCP23017_ADDRESS_22);
    mcp23017_iodir(&hmcp22, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(&hmcp22, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);

    mcp23017_init(&hmcp23, &hi2c1, MCP23017_ADDRESS_23);
    mcp23017_iodir(&hmcp23, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(&hmcp23, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);

    Wavetable_Init();
    Synth_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
UI_ScanAndDispatch();

for (int i = 0; i < BUFFER_SIZE; i++)
{
    bufferDMA[i] = 0;
}

HAL_I2S_Transmit_DMA(&hi2s1,
                     (uint16_t*)bufferDMA,
                     BUFFER_SIZE);
while (1)
{
    UI_ScanAndDispatch();

    HAL_Delay(5);   // scan toutes les 5 ms (~200 Hz)
}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};


    __HAL_RCC_PWR_CLK_ENABLE();

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);


    /*
       PLL principal
       HSE 8MHz

       VCO = 8 / 8 * 336 = 336MHz
       SYSCLK = 336 / 4 = 84MHz
    */

    RCC_OscInitStruct.OscillatorType =
        RCC_OSCILLATORTYPE_HSE;

    RCC_OscInitStruct.HSEState =
        RCC_HSE_ON;

    RCC_OscInitStruct.PLL.PLLState =
        RCC_PLL_ON;

    RCC_OscInitStruct.PLL.PLLSource =
        RCC_PLLSOURCE_HSE;

    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 7;


    if(HAL_RCC_OscConfig(&RCC_OscInitStruct)!=HAL_OK)
        Error_Handler();


    /*
       PLLI2S

       8 / 8 * 192 = 192MHz
       192 / 5 = 38.4MHz I2S clock

       Le prescaler I2S donnera 44.1kHz
    */

    PeriphClkInitStruct.PeriphClockSelection =
        RCC_PERIPHCLK_I2S;

    PeriphClkInitStruct.PLLI2S.PLLI2SN = 192;
    PeriphClkInitStruct.PLLI2S.PLLI2SR = 5;


    if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct)!=HAL_OK)
        Error_Handler();



    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;


    RCC_ClkInitStruct.SYSCLKSource =
        RCC_SYSCLKSOURCE_PLLCLK;

    RCC_ClkInitStruct.AHBCLKDivider =
        RCC_SYSCLK_DIV1;

    RCC_ClkInitStruct.APB1CLKDivider =
        RCC_HCLK_DIV2;

    RCC_ClkInitStruct.APB2CLKDivider =
        RCC_HCLK_DIV1;


    if(HAL_RCC_ClockConfig(
        &RCC_ClkInitStruct,
        FLASH_LATENCY_2)!=HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

 HAL_NVIC_SetPriority(ADC_IRQn, 1, 0);
 HAL_NVIC_EnableIRQ(ADC_IRQn);
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2S1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S1_Init(void)
{

    hi2s1.Instance = SPI1;


    hi2s1.Init.Mode =
        I2S_MODE_MASTER_TX;


    hi2s1.Init.Standard =
        I2S_STANDARD_PHILIPS;


    hi2s1.Init.DataFormat =
        I2S_DATAFORMAT_16B;


    /*
       Pas de MCLK pour UDA1334A
       sauf si ta carte l'utilise
    */

    hi2s1.Init.MCLKOutput =
        I2S_MCLKOUTPUT_DISABLE;


    hi2s1.Init.AudioFreq =
        I2S_AUDIOFREQ_44K;


    hi2s1.Init.CPOL =
        I2S_CPOL_LOW;


    hi2s1.Init.ClockSource =
        I2S_CLOCK_PLL;


    hi2s1.Init.FullDuplexMode =
        I2S_FULLDUPLEXMODE_DISABLE;


    if(HAL_I2S_Init(&hi2s1)!=HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 4;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pins : PC13 PC0 PC1 PC2
                           PC3 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2
                          |GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA2 PA3 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PA8 PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PC10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
