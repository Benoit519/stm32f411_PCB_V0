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
#include <stdio.h>
#include "note.h"
#include "usbd_desc.h"
#include "usbd_midi.h"

#define ATTACK_TIME_MS   20.0f
#define RELEASE_TIME_MS 120.0f

/* Reglages du soufflet a ajuster via le printf pression/repos/gain (serie) :
   DEADZONE = jeu/bruit autour du repos a ignorer (son nul au repos) ;
   PULL/PUSH_MAX_DELTA = ecart de pression observe pour un tire/pousse ferme
   (augmenter si le son plafonne trop bas, diminuer s'il ne monte jamais a fond) ;
   CURVE_EXPONENT > 1 rend les faibles pressions (repos) plus discretes tout en
   gardant un volume max atteignable avec moins d'effort grace au MAX_DELTA reduit.
   Mesures reelles (2026-09-09, capteur repare, repos fige a 2009) : bruit au
   repos ~33, tire a fond ~861, pousse a fond ~1859 - le soufflet pousse
   desormais beaucoup plus que ne tire le capteur. MAX_DELTA fixes sous le
   maximum mesure pour atteindre le volume max avant la butee complete. */
#define BELLOWS_DEADZONE        70u
#define BELLOWS_PULL_MAX_DELTA 550u
#define BELLOWS_PUSH_MAX_DELTA 1000u
#define BELLOWS_CURVE_EXPONENT  1.8f

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
USBD_HandleTypeDef hUsbDeviceFS;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2S1_Init(void);
static void MX_I2C1_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USB_DEVICE_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* -------------------------------------------------------------------------- */
/* Hardware / UI logic: IRQ-driven scan (MCP23017 INTA/INTB -> EXTI)          */
/* -------------------------------------------------------------------------- */
static uint8_t mcp_state[4][2];
volatile uint16_t pressure = 0;

/* Valeur du capteur de pression au repos (aucune force), capturee au premier
   echantillon ADC : tirer augmente la lecture au-dessus de ce repos, pousser
   la diminue en-dessous - un seul et meme capteur, deux sens opposes. */
static volatile uint16_t pressure_rest = 2048;
static volatile uint8_t  pressure_rest_captured = 0;

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

{22,1,4, HAND_RIGHT, {{"Fa3",  NULL}, WT_ACCORDION}, {{"Do3",  NULL}, WT_ACCORDION}},
{22,1,5, HAND_RIGHT, {{"Lad3",  NULL}, WT_ACCORDION}, {{"Lad3",  NULL}, WT_ACCORDION}},
{22,1,6, HAND_RIGHT, {{"Sold3",  NULL}, WT_ACCORDION}, {{"Red3",  NULL}, WT_ACCORDION}},
{22,1,7, HAND_RIGHT, {{"Fa3",  "Do3"}, WT_ACCORDION}, {{"Mi3",  "Sol3"}, WT_ACCORDION}},

/******** MCP23 ********/

{23,1,0, HAND_RIGHT, {{"Lad3",  "Fa3"}, WT_ACCORDION}, {{"Lad3",  "Fa3"}, WT_ACCORDION}},
{23,1,1, HAND_RIGHT, {{"La8",   NULL}, WT_ACCORDION}, {{"La8",   NULL}, WT_ACCORDION}},
{23,1,2, HAND_RIGHT, {{"Do3",   NULL}, WT_ACCORDION}, {{"Sol3",   NULL}, WT_ACCORDION}},
{23,1,3, HAND_RIGHT, {{"La3",   NULL}, WT_ACCORDION}, {{"Re3",   NULL}, WT_ACCORDION}},
{23,1,4, HAND_RIGHT, {{"Dod3",   NULL}, WT_ACCORDION}, {{"Fad3",   NULL}, WT_ACCORDION}},
{23,1,5, HAND_RIGHT, {{"Do3",   "Sol3"}, WT_ACCORDION}, {{"Sol3",   "Re3"}, WT_ACCORDION}},
{23,1,6, HAND_RIGHT, {{"La3",   "Mi3"}, WT_ACCORDION}, {{"Re3",   "La3"}, WT_ACCORDION}},
{23,1,7, HAND_RIGHT, {{"Dod3",  "La3"}, WT_ACCORDION}, {{"Fad3",  "Dod3"}, WT_ACCORDION}},

{23,0,0, HAND_RIGHT, {{"Sol3",   NULL}, WT_ACCORDION}, {{"Re3",   NULL}, WT_ACCORDION}},
{23,0,1, HAND_RIGHT, {{"Mi3",   NULL}, WT_ACCORDION}, {{"La3",   NULL}, WT_ACCORDION}},
{23,0,2, HAND_RIGHT, {{"Re3",  NULL}, WT_ACCORDION}, {{"Si3",  NULL}, WT_ACCORDION}},
{23,0,3, HAND_RIGHT, {{"Sol3",  "Re3"}, WT_ACCORDION}, {{"Re3",  "La3"}, WT_ACCORDION}},
{23,0,4, HAND_RIGHT, {{"Mi3",  "Si3"}, WT_ACCORDION}, {{"La3",  "Mi3"}, WT_ACCORDION}},
{23,0,5, HAND_RIGHT, {{"Re3",  "La3"}, WT_ACCORDION}, {{"Si3",  "Fad3"}, WT_ACCORDION}},
{23,0,6, HAND_RIGHT, {{"Sol10", NULL}, WT_ACCORDION}, {{"Sol10", NULL}, WT_ACCORDION}}

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

/* -------------------------------------------------------------------------- */
/* IRQ-driven button scan (matches the final PCB wiring, verified from the    */
/* routed netlist): each MCP23017's INTA/INTB pin has its own dedicated EXTI  */
/* line, so a falling edge tells us exactly which chip+port to re-read -      */
/* no more blind polling of all 4 chips every 5 ms.                          */
/*                                                                            */
/*   PA10 (EXTI10) -> MCP20 INTA   PC0  (EXTI0)  -> MCP20 INTB               */
/*   PB12 (EXTI12) -> MCP21 INTA   PC1  (EXTI1)  -> MCP21 INTB               */
/*   PA8  (EXTI8)  -> MCP22 INTA   PC2  (EXTI2)  -> MCP22 INTB               */
/*   PC13 (EXTI13) -> MCP23 INTA   PC3  (EXTI3)  -> MCP23 INTB               */
/* -------------------------------------------------------------------------- */
#define MCP_DIRTY_BIT(mcp_idx, port) (1u << ((mcp_idx) * 2 + (port)))

static volatile uint8_t s_mcp_dirty_mask;
static volatile uint8_t s_scan_pending;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    switch(GPIO_Pin)
    {
    case GPIO_PIN_10: s_mcp_dirty_mask |= MCP_DIRTY_BIT(0, MCP23017_PORTA); break; /* MCP20 INTA */
    case GPIO_PIN_0:  s_mcp_dirty_mask |= MCP_DIRTY_BIT(0, MCP23017_PORTB); break; /* MCP20 INTB */
    case GPIO_PIN_12: s_mcp_dirty_mask |= MCP_DIRTY_BIT(1, MCP23017_PORTA); break; /* MCP21 INTA */
    case GPIO_PIN_1:  s_mcp_dirty_mask |= MCP_DIRTY_BIT(1, MCP23017_PORTB); break; /* MCP21 INTB */
    case GPIO_PIN_8:  s_mcp_dirty_mask |= MCP_DIRTY_BIT(2, MCP23017_PORTA); break; /* MCP22 INTA */
    case GPIO_PIN_2:  s_mcp_dirty_mask |= MCP_DIRTY_BIT(2, MCP23017_PORTB); break; /* MCP22 INTB */
    case GPIO_PIN_13: s_mcp_dirty_mask |= MCP_DIRTY_BIT(3, MCP23017_PORTA); break; /* MCP23 INTA */
    case GPIO_PIN_3:  s_mcp_dirty_mask |= MCP_DIRTY_BIT(3, MCP23017_PORTB); break; /* MCP23 INTB */
    default: return;
    }

    s_scan_pending = 1;
}

/* Scanne le bus I2C1 (adresses 7 bits 0x03-0x77) et imprime qui repond.
   Sert a verifier que les 4 MCP23017 sont bien vus a 0x20/0x21/0x22/0x23
   (un strap A0/A1/A2 faux ferait repondre la puce a une autre adresse,
   ou en collision avec une autre puce deja presente). */
static void I2C_ScanBus(void)
{
    printf("Scan I2C1...\r\n");
    uint8_t found = 0;

    for(uint16_t addr7 = 0x03; addr7 <= 0x77; addr7++)
    {
        if(HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr7 << 1), 2, 5) == HAL_OK)
        {
            printf("  -> peripherique repond en 0x%02X\r\n", addr7);
            found++;
        }
    }

    printf("Scan I2C1 termine : %u peripherique(s) detecte(s) (4 attendus: 0x20,0x21,0x22,0x23)\r\n", found);
}

/* Initialise un MCP23017 et arme l'IRQ sur ses 16 broches, puis relit GPINTEN
   pour confirmer par I2C que la config a bien ete prise (glitch bus au power-up,
   adresse fausse, etc. sinon aucune touche de cette puce ne remontera jamais). */
static void MCP_Init_WithIRQ(MCP23017_HandleTypeDef *hdev, uint16_t addr, const char *label)
{
    mcp23017_init(hdev, &hi2c1, addr);
    mcp23017_iodir(hdev, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(hdev, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);
    mcp23017_intcon(hdev, MCP23017_PORTA, 0x00);
    mcp23017_intcon(hdev, MCP23017_PORTB, 0x00);
    mcp23017_gpinten(hdev, MCP23017_PORTA, 0xFF);
    mcp23017_gpinten(hdev, MCP23017_PORTB, 0xFF);

    uint8_t gpintenA = 0, gpintenB = 0;
    HAL_StatusTypeDef stA = mcp23017_read(hdev, MCP23017_REG_GPINTENA | MCP23017_PORTA, &gpintenA);
    HAL_StatusTypeDef stB = mcp23017_read(hdev, MCP23017_REG_GPINTENA | MCP23017_PORTB, &gpintenB);

    if(stA != HAL_OK || stB != HAL_OK || gpintenA != 0xFF || gpintenB != 0xFF)
    {
        printf("ATTENTION MCP%s (0x%02X): IRQ non confirmee ! GPINTENA=0x%02X(st%d) GPINTENB=0x%02X(st%d)\r\n",
               label, addr, gpintenA, stA, gpintenB, stB);
    }
    else
    {
        printf("MCP%s (0x%02X) OK, IRQ armee sur les 16 broches\r\n", label, addr);
    }
}

/* Relit uniquement les ports MCP signales par l'IRQ (mask = bits MCP_DIRTY_BIT) */
static void MCP_Read_Dirty(uint8_t mask)
{
    MCP23017_HandleTypeDef *chips[4] = { &hmcp20, &hmcp21, &hmcp22, &hmcp23 };

    for(int i = 0; i < 4; i++)
    {
        if(mask & MCP_DIRTY_BIT(i, MCP23017_PORTA))
        {
            mcp_state[i][MCP23017_PORTA] = mcp23017_read_gpio_int(chips[i], MCP23017_PORTA);
        }
        if(mask & MCP_DIRTY_BIT(i, MCP23017_PORTB))
        {
            mcp_state[i][MCP23017_PORTB] = mcp23017_read_gpio_int(chips[i], MCP23017_PORTB);
        }
    }
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
/* Convertit un nom de note (table note.c, La4=440Hz) en numero MIDI standard
   (60 = Do4 = C4). Renvoie 0xFF si la note est inconnue. */
static uint8_t NoteNameToMidi(const char *name)
{
    float freq = Note_GetFrequency(name);
    if(freq <= 0.0f)
    {
        return 0xFF;
    }

    int midi = (int)(69.0f + 12.0f * log2f(freq / 440.0f) + 0.5f);
    if(midi < 0)   midi = 0;
    if(midi > 127) midi = 127;

    return (uint8_t)midi;
}

/* Convertit la pression brute en intensite 0..1 selon le sens du soufflet :
   tirer -> plus fort au-dessus du repos, pousser -> plus fort en-dessous
   (le capteur est moins presse quand on pousse que quand on tire).
   Une zone morte autour du repos evite tout son au repos, et l'ecart max
   attendu (au lieu de toute la plage ADC) permet d'atteindre le volume max
   avec une pression ferme réaliste plutot qu'avec toute la plage du capteur. */
static float Bellows_Gain(void)
{
    float delta;
    float max_delta;

    if(bellows_mode == MODE_PULL)
    {
        delta     = (float)pressure - (float)pressure_rest;
        max_delta = (float)BELLOWS_PULL_MAX_DELTA;
    }
    else
    {
        delta     = (float)pressure_rest - (float)pressure;
        max_delta = (float)BELLOWS_PUSH_MAX_DELTA;
    }

    float range = max_delta - (float)BELLOWS_DEADZONE;
    float gain  = (range > 1.0f) ? (delta - (float)BELLOWS_DEADZONE) / range : 0.0f;

    if(gain < 0.0f) gain = 0.0f;
    if(gain > 1.0f) gain = 1.0f;

    /* Courbe en puissance : abaisse le volume pour les faibles pressions
       (repos/legers appuis plus discrets) sans changer les extremes 0 et 1. */
    gain = powf(gain, BELLOWS_CURVE_EXPONENT);

    return gain;
}

/* Main gauche -> canal MIDI 0, main droite -> canal MIDI 1.
   Velocite derivee de la pression du soufflet (expressivite). */
static void MIDI_NoteEvent(const char *note, Hand hand, uint8_t note_on)
{
    uint8_t midi_note = NoteNameToMidi(note);
    if(midi_note == 0xFF)
    {
        return;
    }

    uint8_t channel  = (hand == HAND_LEFT) ? 0 : 1;
    uint8_t velocity = (uint8_t)(1 + Bellows_Gain() * 126.0f);

    if(note_on)
    {
        USBD_MIDI_SendPacket(&hUsbDeviceFS, 0, MIDI_CIN_NOTE_ON,
                              MIDI_STATUS_NOTE_ON | channel, midi_note, velocity);
    }
    else
    {
        USBD_MIDI_SendPacket(&hUsbDeviceFS, 0, MIDI_CIN_NOTE_OFF,
                              MIDI_STATUS_NOTE_OFF | channel, midi_note, 0);
    }
}

static void Dispatch_Buttons(void)
{
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

            printf("Bouton %d presse (MCP%d port%c bit%d, main %s) -> %s%s%s "
                   "[pression=%u repos=%u sens=%s gain=%d%%]\r\n",
                   i,
                   buttons[i].mcp,
                   (port == MCP23017_PORTA) ? 'A' : 'B',
                   bit,
                   (buttons[i].hand == HAND_LEFT) ? "gauche" : "droite",
                   sound->notes[0] ? sound->notes[0] : "",
                   sound->notes[1] ? "+" : "",
                   sound->notes[1] ? sound->notes[1] : "",
                   pressure, pressure_rest,
                   (bellows_mode == MODE_PULL) ? "tire" : "pousse",
                   (int)(Bellows_Gain() * 100.0f));

            for(int n = 0; n < 2; n++)
            {
                if(sound->notes[n] != NULL)
                {
                    NoteOn(
                        sound->notes[n],
                        buttons[i].hand,
                        sound->wavetable
                    );

                    MIDI_NoteEvent(sound->notes[n], buttons[i].hand, 1);
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

                        MIDI_NoteEvent(sound->notes[n], buttons[i].hand, 0);
                    }
                }
            }


            active_sound[i] = NULL;
        }


        /*
            CHANGEMENT DE SENS (bouton toujours enfonce)
            Pas de dependance a un flanc precis : a chaque dispatch, un bouton
            tenu revalide que son son actif correspond bien au sens courant du
            soufflet, et corrige sinon (auto-correction, robuste meme si le
            changement de sens a ete traite lors d'un cycle precedent).
        */

        else if(state && previous_buttons[i] && active_sound[i] != NULL)
        {
            ButtonSound *old_sound = active_sound[i];
            ButtonSound *new_sound =
                (bellows_mode == MODE_PUSH) ? &buttons[i].push : &buttons[i].pull;

            if(new_sound != old_sound)
            {
                printf("Bouton %d : changement de sens en tenant -> %s%s%s\r\n",
                       i,
                       new_sound->notes[0] ? new_sound->notes[0] : "",
                       new_sound->notes[1] ? "+" : "",
                       new_sound->notes[1] ? new_sound->notes[1] : "");

                for(int n = 0; n < 2; n++)
                {
                    if(old_sound->notes[n] == NULL) continue;

                    uint8_t kept_in_new = 0;
                    for(int m = 0; m < 2; m++)
                    {
                        if(new_sound->notes[m] != NULL &&
                           strcmp(new_sound->notes[m], old_sound->notes[n]) == 0)
                        {
                            kept_in_new = 1;
                            break;
                        }
                    }

                    if(!kept_in_new &&
                       !IsNoteHeldByOtherButton(i, old_sound->notes[n], buttons[i].hand))
                    {
                        NoteOff(old_sound->notes[n], buttons[i].hand, old_sound->wavetable);
                        MIDI_NoteEvent(old_sound->notes[n], buttons[i].hand, 0);
                    }
                }

                active_sound[i] = new_sound;

                for(int n = 0; n < 2; n++)
                {
                    if(new_sound->notes[n] == NULL) continue;

                    uint8_t already_on = 0;
                    for(int m = 0; m < 2; m++)
                    {
                        if(old_sound->notes[m] != NULL &&
                           strcmp(old_sound->notes[m], new_sound->notes[n]) == 0)
                        {
                            already_on = 1;
                            break;
                        }
                    }

                    if(!already_on)
                    {
                        NoteOn(new_sound->notes[n], buttons[i].hand, new_sound->wavetable);
                        MIDI_NoteEvent(new_sound->notes[n], buttons[i].hand, 1);
                    }
                }
            }
        }


        previous_buttons[i] = state;
    }
}

/* Scan complet (lecture I2C des 4 MCP + dispatch) : utilise uniquement pour
   l'etat initial, avant que les IRQ EXTI ne prennent le relais. */
static void UI_ScanAndDispatch(void)
{
    MCP_Read_All();
    Dispatch_Buttons();
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
    float gain = Bellows_Gain();

    /* I2S = trames stereo L/R : 2 entrees buffer par echantillon audio.
       N'avancer le DDS qu'une fois par trame, sinon la frequence percue double
       (octave trop aigue) puisque la phase progresserait 2x plus vite que le temps reel. */
    for(uint32_t i = 0; i < samples; i += 2)
    {
        float sample = 0.0f;

        for(int v = 0; v < MAX_VOICES; v++)
        {
            if(voices[v].active)
            {
                /* Lecture wavetable DDS avec interpolation lineaire :
                   bits [31:23] = index (0-511), bits [22:0] = fraction inter-echantillon.
                   Supprime le bruit de quantification "en escalier" du nearest-neighbor,
                   surtout audible sur les notes aigues (grand phase_inc_nom). */
                uint32_t acc = voices[v].phase_acc;
                uint16_t index = (uint16_t)(acc >> 23);
                uint16_t index_next = (index + 1) & (WAVETABLE_SIZE - 1);
                float frac = (float)(acc & 0x7FFFFFu) * (1.0f / 8388608.0f);

                float s0 = (float)voices[v].wave[index];
                float s1 = (float)voices[v].wave[index_next];
                float wave_sample = s0 + (s1 - s0) * frac;

                float envelope =
                    Envelope_Update(&voices[v]);

                sample +=
                    (wave_sample / 32768.0f) *
                    voices[v].amplitude *
                    envelope;

                voices[v].phase_acc += voices[v].phase_inc_nom;
            }
        }

        float output = sample * gain * AMPLITUDE;

        if(output > 32767.0f)  output = 32767.0f;
        if(output < -32768.0f) output = -32768.0f;

        int16_t out16 = (int16_t)output;
        buffer[i]     = out16;   /* canal gauche */
        buffer[i + 1] = out16;   /* canal droit (duplication mono -> stereo) */
    }
}

/* Redirige printf() vers l'UART2 (115200 8N1) - cf. syscalls.c: _write() -> __io_putchar() */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* Capture au boot, apres quelques echantillons de "settle" (le tout premier
   peut etre un transitoire ADC/capteur pas encore stabilise) : avec un capteur
   dont l'ecart tire/pousse reel ne fait que quelques dizaines/centaines de
   counts, une capture instable donne un repos fausse qui peut rendre tire ET
   pousse silencieux (delta insuffisant dans les deux sens depuis ce repos). */
#define PRESSURE_REST_SETTLE_SAMPLES 20u
static uint16_t pressure_rest_settle_count = 0;

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1)
    {
        pressure = HAL_ADC_GetValue(hadc);

        if(!pressure_rest_captured)
        {
            if(pressure_rest_settle_count < PRESSURE_REST_SETTLE_SAMPLES)
            {
                pressure_rest_settle_count++;
            }
            else
            {
                pressure_rest = pressure;
                pressure_rest_captured = 1;
            }
        }
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
  MX_ADC1_Init();
  MX_USART2_UART_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
    printf("Hello world\r\n");
    I2C_ScanBus();
    MCP_Init_WithIRQ(&hmcp20, MCP23017_ADDRESS_20, "20");
    /* Strapping A2/A1/A0 reel sur le PCB (verifie sur le netlist route) = 1/0/0
       => adresse I2C reelle 0x24, pas 0x21 comme l'etiquette schema le laisse
       penser. On garde le handle/l'index logique "21" (table de boutons,
       MCP_Index()) et on adresse juste la vraie puce. */
    MCP_Init_WithIRQ(&hmcp21, MCP23017_ADDRESS_24, "21 (adresse reelle 0x24)");
    MCP_Init_WithIRQ(&hmcp22, MCP23017_ADDRESS_22, "22");
    MCP_Init_WithIRQ(&hmcp23, MCP23017_ADDRESS_23, "23");

    Wavetable_Init();
    Synth_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
UI_ScanAndDispatch();   /* etat initial, avant que les IRQ ne prennent le relais */
s_mcp_dirty_mask = 0;
s_scan_pending = 0;

for (int i = 0; i < BUFFER_SIZE; i++)
{
    bufferDMA[i] = 0;
}

HAL_I2S_Transmit_DMA(&hi2s1,
                     (uint16_t*)bufferDMA,
                     BUFFER_SIZE);

uint32_t last_full_scan = HAL_GetTick();
uint32_t last_pressure_print = HAL_GetTick();

while (1)
{
    if (s_scan_pending)
    {
        __disable_irq();
        uint8_t mask = s_mcp_dirty_mask;
        s_mcp_dirty_mask = 0;
        s_scan_pending = 0;
        __enable_irq();

        MCP_Read_Dirty(mask);
        Dispatch_Buttons();
    }

    /* Filet de securite : un rebalayage complet toutes les 50 ms, au cas ou
       une IRQ aurait ete manquee (glitch I2C au boot, edge EXTI rate, etc.).
       Garantit qu'aucune touche ne reste jamais "bloquee", tout en restant
       ~100x moins bavard sur le bus I2C que l'ancien polling a 5 ms. */
    if ((HAL_GetTick() - last_full_scan) >= 50)
    {
        last_full_scan = HAL_GetTick();
        MCP_Read_All();
        Dispatch_Buttons();
    }

    /* Trace de reglage du soufflet (pas de debugueur dispo) : a retirer une
       fois pressure_rest / le mapping gain valides sur le materiel reel. */
    if ((HAL_GetTick() - last_pressure_print) >= 300)
    {
        last_pressure_print = HAL_GetTick();
        printf("pression=%u repos=%u sens=%s gain=%d%%\r\n",
               pressure, pressure_rest,
               (bellows_mode == MODE_PULL) ? "tire" : "pousse",
               (int)(Bellows_Gain() * 100.0f));
    }

    __WFI();   /* dort jusqu'a la prochaine IRQ (bouton MCP, audio I2S/DMA, ADC) */
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
  * @brief USART2 Initialization Function (debug console, 115200 8N1)
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USB Device (MIDI class) Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_DEVICE_Init(void)
{
    if (USBD_Init(&hUsbDeviceFS, &MIDI_Desc, 0) != USBD_OK)
    {
        Error_Handler();
    }
    if (USBD_RegisterClass(&hUsbDeviceFS, USBD_MIDI_CLASS) != USBD_OK)
    {
        Error_Handler();
    }
    if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
    {
        Error_Handler();
    }
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

  /* EXTI : 8 lignes INTA/INTB des MCP23017 (mapping verifie sur le netlist du PCB routé).
     Priorite 2 : moins critique que le flux audio (I2S DMA=0, ADC=1). */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);          /* PC0  -> MCP20 INTB */

  HAL_NVIC_SetPriority(EXTI1_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);          /* PC1  -> MCP21 INTB */

  HAL_NVIC_SetPriority(EXTI2_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);          /* PC2  -> MCP22 INTB */

  HAL_NVIC_SetPriority(EXTI3_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);          /* PC3  -> MCP23 INTB */

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);        /* PA8  -> MCP22 INTA */

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);      /* PA10 -> MCP20 INTA, PB12 -> MCP21 INTA, PC13 -> MCP23 INTA */
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
