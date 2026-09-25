

"""
Générateur de wavetables fidèle à l'enregistrement original.

Architecture :

    WAV
      |
      +--> estimation F0
      |
      +--> recherche d'une zone stable
      |
      +--> extraction de plusieurs périodes
      |
      +--> alignement de phase
      |
      +--> moyenne pondérée
      |
      +--> suppression DC
      |
      +--> correction de raccord minimale
      |
      +--> 512 points
      |
      +--> normalisation commune
      |
      +--> accordion_tables.h

IMPORTANT
---------

Cette version ne fabrique PAS artificiellement :

    44 harmoniques
    11 harmoniques
    2 harmoniques

Le contenu spectral de chaque WAV est conservé.

Le STM32 utilisera principalement la table BL0.

Les six notes servent ensuite de points de référence :

    DO3
      |
    SOL3
      |
    DO4
      |
    SOL4
      |
    DO5
      |
    SOL5

Le moteur STM32 interpole entre ces six formes d'onde.

Dépendances :

    pip install numpy soundfile
"""

from __future__ import annotations

import csv
import math
import re
from pathlib import Path

import numpy as np
import soundfile as sf


# ============================================================================
# CONFIGURATION
# ============================================================================

SAMPLE_RATE = 44100

#
# Nombre de périodes utilisées pour construire la moyenne.
#
# Chaque table garde sa taille naturelle (periode mesuree, pas de
# reechantillonnage fixe) : le band-limiting + reechantillonnage vers
# WAVETABLE_SIZE (512) est fait au runtime par Wavetable_Init() (wavetable.c).
#
NOMBRE_PERIODES_MOYENNE = 6

#
# Fenêtre utilisée pour mesurer F0.
#
# Une seule fenêtre fixe n'est pas fiable : selon l'enregistrement, l'anche
# peut mettre plus ou moins longtemps à se stabiliser après l'attaque (ex.
# do5.wav mesuré à 456 Hz au lieu de ~523 Hz le 2026-09-23 avec la seule
# fenêtre 0.20-0.45s, alors que le son était stable et juste dès 0.30s).
# On essaie donc plusieurs fenêtres et on garde la plus fiable qui tombe
# dans la tolérance F0 (voir FENETRES_CANDIDATES_S ci-dessous).
#
FENETRE_FREQUENCE_DEBUT_S = 0.20
FENETRE_FREQUENCE_DUREE_S = 0.25

#
# Duree d'analyse F0 pour les fichiers d'ATTAQUE : plus courte que le
# maintien, pour rester localisee et ne pas melanger dans une seule fenetre
# une portion encore transitoire avec la portion deja stabilisee (l'anche
# peut encore glisser en hauteur pendant les 100-150 premieres ms).
#
FENETRE_FREQUENCE_DUREE_ATTAQUE_S = 0.10

FENETRES_CANDIDATES_S = (
    0.20,
    0.35,
    0.50,
    0.70,
    0.90,
    1.10,
)

#
# Idem, pour les fichiers d'ATTAQUE ("a") : ceux-ci peuvent etre tres courts
# (juste l'onset, parfois <0.25s) - les fenetres ci-dessus commenceraient
# deja hors du fichier. On demarre donc bien plus tot, et on couvre aussi
# un peu plus tard (0.16/0.20s) : le tout debut peut encore etre domine par
# un transitoire (ex. harmonique haute avant que le fondamental ne se
# stabilise), la fenetre utile n'est pas toujours la toute premiere.
#
FENETRES_CANDIDATES_ATTAQUE_S = (
    0.02,
    0.05,
    0.08,
    0.12,
    0.16,
    0.20,
    0.30,
)

#
# Tolérance de validation F0.
#
FREQUENCE_MIN_RATIO = 0.90
FREQUENCE_MAX_RATIO = 1.10

#
# Recherche de la zone stable (maintien - fichier "b").
#
DEBUT_RECHERCHE_S = 0.10
RECHERCHE_ZONE_STABLE_S = 0.25

#
# Recherche de la zone d'ATTAQUE (fichier "a") : fenetre volontairement
# precoce et etroite - on veut le timbre du debut de note (transitoire),
# pas la partie deja stabilisee que cible la recherche ci-dessus.
#
DEBUT_RECHERCHE_ATTAQUE_S = 0.02
RECHERCHE_ZONE_ATTAQUE_S = 0.10

#
# Nombre de positions testées.
#
PAS_RECHERCHE_FRACTION_PERIODE = 4.0

#
# Alignement de phase.
#
ALIGNEMENT_PHASE = True
ALIGNEMENT_ZONE = 96

#
# Poids des périodes.
#
POIDS_PERIODES = np.array(
    [
        0.10,
        0.12,
        0.16,
        0.18,
        0.20,
        0.24,
    ],
    dtype=np.float64
)

#
# Variation naturelle.
#
# Par défaut désactivée.
#
# Pourquoi ?
#
# Une wavetable périodique doit être stable.
# Une variation temporelle doit plutôt être recréée par plusieurs tables
# ou par une modulation contrôlée du moteur.
#
CONSERVER_VARIATION_NATURELLE = False
VARIATION_NATURELLE = 0.05

#
# Raccord périodique.
#
# Nous faisons seulement une correction légère si le raccord est réellement
# problématique.
#
CORRIGER_RACCORD_FINAL = True

RACCORD_ZONE_MAX = 16
RACCORD_CORRECTION_MAX = 0.10

#
# Normalisation.
#
# IMPORTANT :
#
# Une seule normalisation commune est appliquée à toutes les notes.
#
# On NE normalise PAS chaque note individuellement.
#
NORMALISATION_GLOBALE = True

AMPLITUDE_CIBLE = 30000.0

#
# Sécurité.
#
MIN_TABLE_SIZE = 40
MAX_TABLE_SIZE = 4096
AMPLITUDE_MIN_SIGNAL = 1e-8

#
# Export.
#
VALEURS_PAR_LIGNE = 12

#
# Fichiers : DEUX enregistrements par note - "a" = attaque (debut de note,
# avant que l'enveloppe/le timbre soit stable), "b" = maintien (partie
# constante). Ex. do3a.wav + do3b.wav. Les deux fichiers doivent contenir
# la meme note (verifie par verifier_coherence_note).
#
NOTES_BASE = [
    "do3",
    "sol3",
    "do4",
    "sol4",
    "do5",
    "sol5",
]

SUFFIXE_ATTAQUE = "a"
SUFFIXE_MAINTIEN = "b"

#
# Override manuel de la frequence theorique attendue pour certains fichiers,
# quand le nom ne correspond pas a la frequence temperee standard (ex.
# accordage particulier de l'instrument). Cle = nom de fichier exact (les
# roles attaque/maintien d'une meme note peuvent avoir des frequences
# differentes - confirme par l'utilisateur le 2026-09-23 pour do4 : le
# maintien sonne ~392 Hz mais l'attaque mesure ~262 Hz, les deux acceptes
# tels quels comme caracteristique de cette anche/cet instrument).
#
FREQUENCES_MANUELLES: dict[str, float] = {
    "do4a.wav": 392.0,
    "do4b.wav": 392.0,
    "sol5a.wav": 392.0,
    "sol5b.wav": 392.0,
}

#
# Ecrit DIRECTEMENT dans le projet firmware : relancer ce script puis
# recompiler (make) suffit, sans copie manuelle. Le script vit dans
# <projet>/scripts/, donc le grand-parent de ce fichier EST la racine
# du projet (contient Core/, Debug/, le .ioc) - robuste si tout le
# dossier repos/ est deplace/clone ailleurs.
#
PROJET_DIR = Path(__file__).resolve().parent.parent

FICHIER_SORTIE_H = PROJET_DIR / "Core" / "Inc" / "accordion_tables.h"
FICHIER_SORTIE_C = PROJET_DIR / "Core" / "Src" / "accordion_tables.c"
FICHIER_RAPPORT = Path(__file__).resolve().parent / "accordion_tables_report.csv"

#
# True = toute erreur arrête le programme.
#
MODE_STRICT = True


# ============================================================================
# NOTES
# ============================================================================

NOTE_SEMITONES = {
    "do": 0,
    "do#": 1,
    "reb": 1,
    "re": 2,
    "re#": 3,
    "mib": 3,
    "mi": 4,
    "fa": 5,
    "fa#": 6,
    "solb": 6,
    "sol": 7,
    "sol#": 8,
    "lab": 8,
    "la": 9,
    "la#": 10,
    "sib": 10,
    "si": 11,
}


# ============================================================================
# UTILITAIRES
# ============================================================================

def frequence_depuis_nom(
    fichier: str | Path,
) -> float:
    """
    Extrait la fréquence théorique d'un fichier :

        do3.wav
        sol4.wav
        do5.wav
    """

    nom = Path(fichier).stem.lower()

    match = re.search(
        r"([a-z]+[#b]?)(-?\d+)",
        nom,
    )

    if not match:
        raise ValueError(
            f"Impossible de trouver la note et l'octave dans : {nom}"
        )

    note = match.group(1)
    octave = int(match.group(2))

    if note not in NOTE_SEMITONES:
        raise ValueError(
            f"Note inconnue : {note}"
        )

    midi = (
        12 * (octave + 1)
        + NOTE_SEMITONES[note]
    )

    return (
        440.0 *
        2.0 ** ((midi - 69) / 12.0)
    )


def nom_note_c(
    fichier: str | Path,
) -> str:
    """
    Transforme :

        sol4.wav

    en :

        sol4
    """

    nom = Path(fichier).stem.lower()

    nom = nom.replace(
        "#",
        "_sharp",
    )

    nom = re.sub(
        r"[^a-zA-Z0-9_]",
        "_",
        nom,
    )

    if not nom:
        raise ValueError(
            f"Nom de note invalide : {fichier}"
        )

    if nom[0].isdigit():
        nom = "_" + nom

    return nom


def macro_note_c(
    fichier: str | Path,
) -> str:
    return nom_note_c(fichier).upper()


# ============================================================================
# LECTURE WAV
# ============================================================================

def preparer_signal(
    fichier: str | Path,
) -> tuple[np.ndarray, int]:
    """
    Charge le WAV en mono et supprime le DC.
    """

    signal, sr = sf.read(
        fichier,
        always_2d=False,
    )

    if sr != SAMPLE_RATE:
        raise ValueError(
            f"{fichier}: sample rate = {sr} Hz, "
            f"attendu = {SAMPLE_RATE} Hz."
        )

    signal = np.asarray(
        signal,
        dtype=np.float64,
    )

    if signal.ndim == 2:
        signal = np.mean(
            signal,
            axis=1,
        )

    if (
        signal.ndim != 1
        or len(signal) == 0
    ):
        raise ValueError(
            f"{fichier}: signal audio invalide."
        )

    signal -= np.mean(signal)

    peak = float(
        np.max(
            np.abs(signal)
        )
    )

    if peak < AMPLITUDE_MIN_SIGNAL:
        raise ValueError(
            f"{fichier}: signal trop faible."
        )

    return signal, sr


# ============================================================================
# ESTIMATION F0
# ============================================================================

def _estimer_f0_dans_fenetre(
    signal: np.ndarray,
    frequence_theorique: float,
    sr: int,
    debut_s: float,
    duree_s: float = FENETRE_FREQUENCE_DUREE_S,
) -> tuple[float, float] | None:
    """
    Tente une estimation F0 par autocorrélation dans UNE fenêtre donnée.

    Renvoie (frequence, confiance) ou None si la fenêtre est inexploitable
    (hors du signal, trop courte, trop faible, plage de lags invalide).
    La confiance est le pic d'autocorrélation normalisé (plus haut = plus
    fiable), utilisée pour départager plusieurs fenêtres valides.
    """

    debut = int(debut_s * sr)
    duree = int(duree_s * sr)
    fin = min(len(signal), debut + duree)

    if fin - debut < max(int(0.05 * sr), 256):
        return None

    x = signal[debut:fin].copy()
    x -= np.mean(x)
    x *= np.hanning(len(x))

    energie = float(np.dot(x, x))

    if energie < 1e-12:
        return None

    f_min = frequence_theorique * FREQUENCE_MIN_RATIO
    f_max = frequence_theorique * FREQUENCE_MAX_RATIO

    lag_min = max(2, int(math.floor(sr / f_max)))
    lag_max = min(int(math.ceil(sr / f_min)), len(x) // 2)

    if lag_min >= lag_max:
        return None

    #
    # Autocorrélation par FFT.
    #
    nfft = 1 << (2 * len(x) - 1).bit_length()
    X = np.fft.rfft(x, n=nfft)
    ac = np.fft.irfft(X * np.conj(X), n=nfft)[:len(x)]
    ac /= max(ac[0], 1e-20)

    zone = ac[lag_min:lag_max + 1]
    lag = lag_min + int(np.argmax(zone))
    confiance = float(zone[lag - lag_min])

    #
    # Interpolation parabolique.
    #
    if 1 <= lag < len(ac) - 1:
        y1, y2, y3 = ac[lag - 1], ac[lag], ac[lag + 1]
        denom = y1 - 2.0 * y2 + y3
        delta = 0.5 * (y1 - y3) / denom if abs(denom) > 1e-12 else 0.0
    else:
        delta = 0.0

    periode = lag + delta

    if periode <= 0:
        return None

    return sr / periode, confiance


def estimer_frequence_reelle(
    signal: np.ndarray,
    frequence_theorique: float,
    sr: int,
    fenetres_candidates: tuple[float, ...] = FENETRES_CANDIDATES_S,
    duree_s: float = FENETRE_FREQUENCE_DUREE_S,
) -> float:
    """
    Estimation de F0 par autocorrélation normalisée, essayée sur plusieurs
    fenêtres temporelles (fenetres_candidates) : une anche peut mettre
    plus ou moins longtemps à se stabiliser après l'attaque selon
    l'enregistrement, donc une seule fenêtre fixe n'est pas fiable.

    Parmi les fenêtres dont le résultat tombe dans la tolérance F0, on
    garde celle avec le pic d'autocorrélation le plus net (confiance
    maximale). Si aucune fenêtre ne tombe dans la tolérance, on renvoie
    quand même la meilleure estimation obtenue : l'appelant
    (extraire_table_unique) fera l'échouer avec un message clair.
    """

    meilleur_resultat: float | None = None
    meilleure_confiance = float("-inf")
    tentatives: list[str] = []

    for debut_s in fenetres_candidates:

        resultat = _estimer_f0_dans_fenetre(
            signal, frequence_theorique, sr, debut_s, duree_s,
        )

        if resultat is None:
            continue

        frequence, confiance = resultat
        ratio = frequence / frequence_theorique
        dans_tolerance = FREQUENCE_MIN_RATIO <= ratio <= FREQUENCE_MAX_RATIO

        tentatives.append(
            f"{debut_s:.2f}s->{frequence:.1f}Hz"
            f"{'(ok)' if dans_tolerance else '(hors tolerance)'}"
        )

        if dans_tolerance and confiance > meilleure_confiance:
            meilleure_confiance = confiance
            meilleur_resultat = frequence

    if meilleur_resultat is not None:
        return meilleur_resultat

    if not tentatives:
        raise ValueError(
            "Signal trop court/faible sur toutes les fenêtres testées."
        )

    raise ValueError(
        "Aucune fenêtre stable trouvée dans la tolérance F0 "
        f"({', '.join(tentatives)})."
    )


# ============================================================================
# EXTRACTION D'UNE PERIODE
# ============================================================================

def extraire_periode_interpolee(
    signal: np.ndarray,
    position: float,
    periode: float,
    taille: int,
) -> np.ndarray:
    """
    Extrait une période complète par interpolation.
    """

    positions = (
        position
        +
        np.arange(
            taille,
            dtype=np.float64,
        )
        *
        periode
        /
        taille
    )

    if (
        positions[0] < 0
        or positions[-1] >= len(signal)
    ):
        raise ValueError(
            "Période demandée en dehors du signal."
        )

    indices = np.arange(
        len(signal),
        dtype=np.float64,
    )

    return np.interp(
        positions,
        indices,
        signal,
    )


def extraire_plusieurs_periodes(
    signal: np.ndarray,
    position: float,
    periode: float,
    nombre_periodes: int,
    taille: int,
) -> np.ndarray:
    periodes = []

    for p in range(
        nombre_periodes
    ):
        position_p = (
            position +
            p * periode
        )

        periodes.append(
            extraire_periode_interpolee(
                signal,
                float(position_p),
                periode,
                taille,
            )
        )

    return np.asarray(
        periodes,
        dtype=np.float64,
    )


# ============================================================================
# SCORE STABILITE
# ============================================================================

def score_stabilite_periodes(
    periodes: np.ndarray,
) -> float:
    """
    Plus faible = plus stable.
    """

    moyenne = np.mean(
        periodes,
        axis=0,
    )

    energie = float(
        np.mean(
            moyenne * moyenne
        )
    )

    if energie < 1e-12:
        return float("inf")

    erreur_amplitude = float(
        np.mean(
            (
                periodes -
                moyenne[None, :]
            ) ** 2
        )
        /
        energie
    )

    pentes = np.diff(
        periodes,
        axis=1,
    )

    pente_moyenne = np.diff(
        moyenne
    )

    energie_pente = (
        float(
            np.mean(
                pente_moyenne ** 2
            )
        )
        + 1e-12
    )

    erreur_pente = float(
        np.mean(
            (
                pentes -
                pente_moyenne[None, :]
            ) ** 2
        )
        /
        energie_pente
    )

    return (
        erreur_amplitude
        +
        0.25 *
        erreur_pente
    )


# ============================================================================
# RECHERCHE ZONE STABLE
# ============================================================================

def trouver_zone_stable(
    signal: np.ndarray,
    frequence: float,
    sr: int,
    nombre_periodes: int,
    debut_recherche_s: float = DEBUT_RECHERCHE_S,
    largeur_recherche_s: float = RECHERCHE_ZONE_STABLE_S,
) -> tuple[float, float]:

    periode = (
        sr /
        frequence
    )

    taille_analyse = int(
        round(periode)
    )

    taille_analyse = max(
        MIN_TABLE_SIZE,
        taille_analyse,
    )

    taille_analyse = min(
        MAX_TABLE_SIZE,
        taille_analyse,
    )

    debut_global = int(
        debut_recherche_s *
        sr
    )

    largeur = int(
        largeur_recherche_s *
        sr
    )

    fin_global = min(
        len(signal)
        - int(
            nombre_periodes *
            periode
        )
        - 2,
        debut_global +
        largeur,
    )

    if fin_global <= debut_global:
        raise ValueError(
            "Le fichier est trop court pour "
            "chercher une zone stable."
        )

    pas = max(
        1,
        int(
            round(
                periode /
                PAS_RECHERCHE_FRACTION_PERIODE
            )
        ),
    )

    meilleur_score = float("inf")
    meilleure_position = float(
        debut_global
    )

    for position in range(
        debut_global,
        fin_global + 1,
        pas,
    ):

        periodes = (
            extraire_plusieurs_periodes(
                signal,
                float(position),
                periode,
                nombre_periodes,
                taille_analyse,
            )
        )

        score = (
            score_stabilite_periodes(
                periodes
            )
        )

        if score < meilleur_score:
            meilleur_score = score
            meilleure_position = float(
                position
            )

    return (
        meilleure_position,
        meilleur_score,
    )


# ============================================================================
# ALIGNEMENT DE PHASE
# ============================================================================

def aligner_periode_sur_reference(
    periode: np.ndarray,
    reference: np.ndarray,
    recherche: int = ALIGNEMENT_ZONE,
) -> np.ndarray:

    n = len(periode)

    if n < 4:
        return periode.copy()

    zone = min(
        recherche,
        n // 4,
    )

    if zone < 4:
        return periode.copy()

    ref = reference[
        :zone
    ]

    meilleur_score = float("inf")
    meilleur_shift = 0

    for shift in range(
        -zone,
        zone + 1,
    ):

        candidate = np.roll(
            periode,
            shift,
        )

        c = candidate[
            :zone
        ]

        score = float(
            np.mean(
                (c - ref) ** 2
            )
        )

        if score < meilleur_score:
            meilleur_score = score
            meilleur_shift = shift

    return np.roll(
        periode,
        meilleur_shift,
    )


# ============================================================================
# CONSTRUCTION TABLE
# ============================================================================

def construire_periode_moyenne(
    signal: np.ndarray,
    position: float,
    frequence: float,
    sr: int,
    nombre_periodes: int,
) -> np.ndarray:

    periode = (
        sr /
        frequence
    )

    taille_source = int(
        round(periode)
    )

    if (
        taille_source <
        MIN_TABLE_SIZE
    ):
        raise ValueError(
            f"Période trop courte : "
            f"{taille_source} samples."
        )

    if (
        taille_source >
        MAX_TABLE_SIZE
    ):
        raise ValueError(
            f"Période trop longue : "
            f"{taille_source} samples."
        )

    periodes = (
        extraire_plusieurs_periodes(
            signal,
            position,
            periode,
            nombre_periodes,
            taille_source,
        )
    )

    reference = (
        periodes[0].copy()
    )

    periodes_alignees = []

    for p in periodes:

        if ALIGNEMENT_PHASE:
            p = (
                aligner_periode_sur_reference(
                    p,
                    reference,
                )
            )

        periodes_alignees.append(
            p
        )

    periodes_alignees = np.asarray(
        periodes_alignees,
        dtype=np.float64,
    )

    poids = np.asarray(
        POIDS_PERIODES,
        dtype=np.float64,
    )

    if len(poids) != nombre_periodes:
        poids = np.linspace(
            1.0,
            2.0,
            nombre_periodes,
            dtype=np.float64,
        )

    poids /= np.sum(
        poids
    )

    moyenne = np.average(
        periodes_alignees,
        axis=0,
        weights=poids,
    )

    #
    # Optionnellement conserver une petite variation naturelle.
    #
    if (
        CONSERVER_VARIATION_NATURELLE
        and
        nombre_periodes >= 2
    ):
        variation = (
            periodes_alignees[-1]
            -
            moyenne
        )

        moyenne += (
            VARIATION_NATURELLE *
            variation
        )

    #
    # Suppression DC.
    #
    moyenne -= np.mean(
        moyenne
    )

    #
    # Pas de rééchantillonnage ici : on garde la taille naturelle de la
    # période (taille_source), comme l'attend accordion_tables.c/.h côté
    # firmware. Le band-limiting + rééchantillonnage vers WAVETABLE_SIZE
    # (512) est fait au runtime par Wavetable_Init() (wavetable.c).
    #
    return moyenne


# ============================================================================
# RACCORDEMENT
# ============================================================================

def score_raccordement(
    table: np.ndarray,
) -> float:

    n = len(table)

    zone = min(
        RACCORD_ZONE_MAX,
        max(
            4,
            n // 16,
        ),
    )

    if n < (
        2 * zone + 2
    ):
        return float("inf")

    debut = table[
        :zone
    ]

    fin = table[
        -zone:
    ]

    erreur_amplitude = float(
        np.mean(
            (debut - fin) ** 2
        )
    )

    pente_debut = np.diff(
        debut
    )

    pente_fin = np.diff(
        fin
    )

    erreur_pente = float(
        np.mean(
            (
                pente_debut -
                pente_fin
            ) ** 2
        )
    )

    return (
        erreur_amplitude
        +
        0.5 *
        erreur_pente
    )


def corriger_raccordement_doucement(
    table: np.ndarray,
) -> np.ndarray:
    """
    Corrige uniquement une petite fraction de la discontinuité.

    Cette fonction ne doit surtout pas remodeler la forme d'onde.
    """

    x = table.copy()

    n = len(x)

    zone = min(
        RACCORD_ZONE_MAX,
        max(
            4,
            n // 32,
        ),
    )

    if n < (
        2 * zone + 2
    ):
        return x

    difference = (
        x[0] -
        x[-1]
    )

    amplitude = max(
        float(
            np.max(
                np.abs(x)
            )
        ),
        1e-12,
    )

    ratio = (
        abs(difference) /
        amplitude
    )

    #
    # Si le raccord est déjà excellent :
    # ne rien faire.
    #
    if ratio < 0.002:
        return x

    correction = (
        difference *
        RACCORD_CORRECTION_MAX
    )

    t = np.linspace(
        0.0,
        1.0,
        zone,
    )

    fenetre = (
        0.5
        -
        0.5 *
        np.cos(
            np.pi * t
        )
    )

    x[:zone] -= (
        correction *
        0.5 *
        (1.0 - fenetre)
    )

    x[-zone:] += (
        correction *
        0.5 *
        fenetre
    )

    return x


# ============================================================================
# NORMALISATION GLOBALE
# ============================================================================

def normaliser_tables_globalement(
    tables: list[np.ndarray],
) -> list[np.ndarray]:
    """
    UNE SEULE normalisation pour toutes les notes.

    C'est essentiel pour conserver les différences naturelles de niveau.
    """

    if not tables:
        return []

    peak_global = max(
        float(
            np.max(
                np.abs(table)
            )
        )
        for table in tables
    )

    if peak_global <= 1e-12:
        return [
            table.copy()
            for table in tables
        ]

    gain = (
        AMPLITUDE_CIBLE /
        peak_global
    )

    return [
        table * gain
        for table in tables
    ]


# ============================================================================
# CONVERSION INT16
# ============================================================================

def convertir_int16(
    table: np.ndarray,
) -> np.ndarray:

    table = np.clip(
        table,
        -32768,
        32767,
    )

    return np.round(
        table
    ).astype(
        np.int16
    )


def _pic_spectral_large_bande(
    signal: np.ndarray,
    sr: int,
    debut_s: float,
) -> float | None:
    """
    Pic FFT dominant SANS restriction de plage (contrairement à
    l'autocorrélation, bornée autour de la fréquence théorique). Sert à
    détecter un fichier mal étiqueté / la mauvaise note (l'autocorrélation
    seule ne peut pas le voir : sa plage de recherche est construite autour
    de la fréquence théorique, donc elle "trouve" toujours quelque chose de
    plausible même si la vraie note est complètement différente).
    """

    debut = int(debut_s * sr)
    fin = min(len(signal), debut + int(FENETRE_FREQUENCE_DUREE_S * sr))

    if fin - debut < max(int(0.05 * sr), 256):
        return None

    x = signal[debut:fin].copy()
    x -= np.mean(x)
    x *= np.hanning(len(x))

    X = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(len(x), 1.0 / sr)
    mag = np.abs(X)
    mag[freqs < 60.0] = 0.0

    if not np.any(mag):
        return None

    return float(freqs[np.argmax(mag)])


def verifier_coherence_note(
    signal: np.ndarray,
    sr: int,
    frequence_theorique: float,
    fichier: str | Path,
    fenetres_candidates: tuple[float, ...] = FENETRES_CANDIDATES_S,
) -> None:
    """
    Vérifie que LA VRAIE note du fichier correspond à ce que son nom promet.

    L'autocorrélation (estimer_frequence_reelle) ne peut mathématiquement
    renvoyer qu'une valeur proche de frequence_theorique (sa plage de
    recherche est bornée autour de cette cible) : elle ne peut donc jamais
    détecter un fichier contenant en réalité une tout autre note. On compare
    ici le pic spectral large bande (non borné) à la fréquence théorique et
    à ses harmoniques/sous-harmoniques usuelles (0.5x/2x/3x, un reed peut
    avoir un harmonique plus fort que le fondamental).
    """

    pics = []

    for debut_s in fenetres_candidates:
        pic = _pic_spectral_large_bande(signal, sr, debut_s)
        if pic is not None:
            pics.append((debut_s, pic))

    if not pics:
        return   # pas assez de signal pour ce contrôle, laisse l'autre validation trancher

    for _debut_s, pic in pics:
        for k in (1.0, 2.0, 3.0, 0.5, 1.0 / 3.0):
            ratio = pic / (k * frequence_theorique)
            if FREQUENCE_MIN_RATIO <= ratio <= FREQUENCE_MAX_RATIO:
                return

    detail = ", ".join(f"{d:.2f}s->{p:.1f}Hz" for d, p in pics)
    raise ValueError(
        f"{fichier}: le contenu spectral ({detail}) ne correspond a "
        f"aucune harmonique plausible de {frequence_theorique:.1f} Hz - "
        "fichier probablement mal etiquete / mauvaise note."
    )


# ============================================================================
# TRAITEMENT D'UNE NOTE
# ============================================================================

def extraire_table_unique(
    fichier_entree: str | Path,
    nom_base: str,
    est_attaque: bool = False,
) -> tuple[np.ndarray, dict]:

    signal, sr = (
        preparer_signal(
            fichier_entree
        )
    )

    #
    # frequence_depuis_nom lit note+octave depuis une chaine "do3" - on
    # passe le nom de base explicite (pas le nom de fichier, qui porte en
    # plus le suffixe a/b) pour eviter toute ambiguite de parsing.
    #
    frequence_theorique = (
        frequence_depuis_nom(
            nom_base
        )
    )

    #
    # Override manuel (FREQUENCES_MANUELLES) : certains fichiers ne suivent
    # pas la frequence temperee standard deduite de leur nom (accordage
    # particulier de l'instrument, confirme par l'utilisateur). Cle = nom
    # de fichier exact (attaque et maintien d'une meme note peuvent differer).
    #
    nom_fichier = Path(fichier_entree).name
    if nom_fichier in FREQUENCES_MANUELLES:
        frequence_theorique = FREQUENCES_MANUELLES[nom_fichier]

    #
    # Les fichiers d'attaque peuvent etre bien plus courts (juste l'onset) :
    # utiliser des fenetres d'analyse F0 plus precoces/etroites pour eux.
    #
    fenetres_f0 = (
        FENETRES_CANDIDATES_ATTAQUE_S if est_attaque else FENETRES_CANDIDATES_S
    )
    duree_f0 = (
        FENETRE_FREQUENCE_DUREE_ATTAQUE_S if est_attaque else FENETRE_FREQUENCE_DUREE_S
    )

    verifier_coherence_note(
        signal,
        sr,
        frequence_theorique,
        fichier_entree,
        fenetres_f0,
    )

    frequence = (
        estimer_frequence_reelle(
            signal,
            frequence_theorique,
            sr,
            fenetres_f0,
            duree_f0,
        )
    )

    erreur_pct = (
        100.0 *
        (
            frequence -
            frequence_theorique
        )
        /
        frequence_theorique
    )

    ratio = (
        frequence /
        frequence_theorique
    )

    if not (
        FREQUENCE_MIN_RATIO
        <= ratio
        <= FREQUENCE_MAX_RATIO
    ):
        raise ValueError(
            f"{fichier_entree}: fréquence mesurée "
            f"{frequence:.3f} Hz trop éloignée de "
            f"{frequence_theorique:.3f} Hz."
        )

    periode = (
        sr /
        frequence
    )

    taille_source = int(
        round(periode)
    )

    if (
        taille_source <
        MIN_TABLE_SIZE
        or
        taille_source >
        MAX_TABLE_SIZE
    ):
        raise ValueError(
            f"{fichier_entree}: période invalide "
            f"({taille_source} samples)."
        )

    #
    # Recherche de la meilleure zone (fenetre precoce/etroite pour l'attaque,
    # fenetre standard pour le maintien - voir DEBUT/RECHERCHE_ZONE_*_S).
    #
    debut_recherche_s = (
        DEBUT_RECHERCHE_ATTAQUE_S if est_attaque else DEBUT_RECHERCHE_S
    )
    largeur_recherche_s = (
        RECHERCHE_ZONE_ATTAQUE_S if est_attaque else RECHERCHE_ZONE_STABLE_S
    )

    position, score_stable = (
        trouver_zone_stable(
            signal,
            frequence,
            sr,
            NOMBRE_PERIODES_MOYENNE,
            debut_recherche_s,
            largeur_recherche_s,
        )
    )

    #
    # Construction.
    #
    table = (
        construire_periode_moyenne(
            signal,
            position,
            frequence,
            sr,
            NOMBRE_PERIODES_MOYENNE,
        )
    )

    #
    # Raccord.
    #
    score_raccord_avant = (
        score_raccordement(
            table
        )
    )

    if CORRIGER_RACCORD_FINAL:
        table = (
            corriger_raccordement_doucement(
                table
            )
        )

    #
    # DC final.
    #
    table -= np.mean(
        table
    )

    info = {
        "frequence_theorique":
            frequence_theorique,

        "frequence_mesuree":
            frequence,

        "erreur_frequence_pct":
            erreur_pct,

        "periode":
            periode,

        "taille":
            len(table),

        "position":
            position,

        "score_stable":
            score_stable,

        "score_raccord":
            score_raccord_avant,
    }

    return table, info


# ============================================================================
# EXPORT C
# ============================================================================

def generer_accordion_tables_h(
    tables: list[
        tuple[str, str, np.ndarray, dict]
    ],
    fichier_sortie: str | Path,
) -> None:
    """
    Header de déclarations (extern), format identique à l'existant
    Core/Inc/accordion_tables.h : une taille + un tableau par note/rôle.

    `tables` : liste de (nom_c, fichier_source, table, info) - nom_c est
    l'identifiant C deja calcule par l'appelant (ex. "do3" pour le maintien,
    "do3_attack" pour l'attaque), PAS derive du nom de fichier ici (celui-ci
    porte en plus le suffixe a/b, non pertinent pour le nom du symbole C).

    Ne déclare PAS wavetable_accordion/WAVETABLE_SIZE/ACCORDION_NUM_* :
    ce sont wavetable.h/wavetable.c qui les définissent et construisent
    les tables band-limited au runtime (Wavetable_Init), à partir des
    tableaux déclarés ici.
    """

    lignes = []

    lignes.append("#ifndef ACCORDION_TABLES_H")
    lignes.append("#define ACCORDION_TABLES_H")
    lignes.append("")
    lignes.append("#include <stdint.h>")
    lignes.append("")
    lignes.append("/* Taille de chaque periode source (echantillons mesures) */")

    for nom_c, _fichier, table, _info in tables:
        macro = nom_c.upper()
        lignes.append(f"#define {macro}_SIZE   {len(table)}")

    lignes.append("")

    for nom_c, _fichier, _table, _info in tables:
        macro = nom_c.upper()
        lignes.append(f"extern const int16_t {nom_c} [{macro}_SIZE];")

    lignes.append("")
    lignes.append("#endif /* ACCORDION_TABLES_H */")
    lignes.append("")

    Path(fichier_sortie).write_text(
        "\n".join(lignes),
        encoding="utf-8",
    )


def generer_accordion_tables_c(
    tables: list[
        tuple[str, str, np.ndarray, dict]
    ],
    fichier_sortie: str | Path,
) -> None:
    """
    Définitions des tableaux déclarés dans accordion_tables.h, une
    période complète (taille naturelle) par note/rôle.
    """

    lignes = []

    lignes.append('#include "accordion_tables.h"')
    lignes.append("")
    lignes.append(
        "/* Genere automatiquement par generateur_wavetable_accordeon_ameliore.py */"
    )
    lignes.append("")

    for nom_c, _fichier, table, _info in tables:

        macro = nom_c.upper()

        lignes.append(f"const int16_t {nom_c}[{macro}_SIZE] =")
        lignes.append("{")

        for i in range(0, len(table), VALEURS_PAR_LIGNE):

            bloc = table[i:i + VALEURS_PAR_LIGNE]
            texte = ",".join(str(int(x)) for x in bloc)

            if i + VALEURS_PAR_LIGNE < len(table):
                texte += ","

            lignes.append(f"    {texte}")

        lignes.append("};")
        lignes.append("")

    Path(fichier_sortie).write_text(
        "\n".join(lignes),
        encoding="utf-8",
    )


# ============================================================================
# RAPPORT
# ============================================================================

def generer_rapport_csv(
    tables: list[
        tuple[str, str, np.ndarray, dict]
    ],
    fichier_sortie: str | Path,
) -> None:

    champs = [
        "nom_c",
        "fichier",
        "frequence_theorique_hz",
        "frequence_mesuree_hz",
        "erreur_frequence_pct",
        "periode_samples",
        "taille_table",
        "position_s",
        "score_stabilite",
        "score_raccord",
        "premier",
        "dernier",
        "ecart_raccord",
    ]

    with open(
        fichier_sortie,
        "w",
        newline="",
        encoding="utf-8",
    ) as f:

        writer = csv.DictWriter(
            f,
            fieldnames=champs,
        )

        writer.writeheader()

        for (
            nom_c,
            fichier,
            table,
            info,
        ) in tables:

            writer.writerow({
                "nom_c":
                    nom_c,

                "fichier":
                    fichier,

                "frequence_theorique_hz":
                    f"{info['frequence_theorique']:.9f}",

                "frequence_mesuree_hz":
                    f"{info['frequence_mesuree']:.9f}",

                "erreur_frequence_pct":
                    f"{info['erreur_frequence_pct']:.4f}",

                "periode_samples":
                    f"{info['periode']:.9f}",

                "taille_table":
                    len(table),

                "position_s":
                    f"{info['position'] / SAMPLE_RATE:.6f}",

                "score_stabilite":
                    f"{info['score_stable']:.10f}",

                "score_raccord":
                    f"{info['score_raccord']:.10f}",

                "premier":
                    int(table[0]),

                "dernier":
                    int(table[-1]),

                "ecart_raccord":
                    f"{float(table[0] - table[-1]):+.3f}",
            })


# ============================================================================
# VERIFICATION
# ============================================================================

def verifier_notes(
    notes_base: list[str],
) -> None:

    if not notes_base:
        raise ValueError(
            "La liste NOTES_BASE est vide."
        )

    if len(notes_base) != 6:
        raise ValueError(
            "Ce générateur attend exactement "
            "6 notes : do3, sol3, do4, sol4, do5, sol5."
        )

    vus = set()

    for base in notes_base:

        if base in vus:
            raise ValueError(
                f"Note dupliquée : {base}"
            )

        vus.add(base)

        for suffixe in (SUFFIXE_ATTAQUE, SUFFIXE_MAINTIEN):

            fichier = f"{base}{suffixe}.wav"

            if not Path(fichier).exists():
                raise FileNotFoundError(
                    f"Fichier WAV introuvable : {fichier} "
                    f"(attendu : une paire attaque/maintien par note, "
                    f"ex. {base}{SUFFIXE_ATTAQUE}.wav + "
                    f"{base}{SUFFIXE_MAINTIEN}.wav)."
                )


def verifier_projet_dir() -> None:
    if not PROJET_DIR.is_dir():
        raise FileNotFoundError(
            f"Projet firmware introuvable : {PROJET_DIR} "
            "(verifier PROJET_DIR en haut du script)."
        )


# ============================================================================
# TRAITEMENT GLOBAL
# ============================================================================

def traiter_toutes_les_notes(
    notes_base: list[str],
    fichier_sortie_h: str | Path,
    fichier_sortie_c: str | Path,
) -> list[
    tuple[str, str, np.ndarray, dict]
]:

    verifier_projet_dir()

    verifier_notes(
        notes_base
    )

    #
    # Une entree par (note, role) - role="maintien" garde le nom C existant
    # (do3, sol3...), role="attack" utilise <base>_attack (nouveaux symboles,
    # cf. generer_accordion_tables_h/_c).
    #
    taches = []
    for base in notes_base:
        taches.append((base, f"{base}{SUFFIXE_ATTAQUE}.wav", True, f"{base}_attack"))
        taches.append((base, f"{base}{SUFFIXE_MAINTIEN}.wav", False, base))

    tables_brutes = []

    erreurs = []

    print()
    print("=" * 72)
    print(
        "       GENERATION ACCORDION_TABLES.H (attaque + maintien)"
    )
    print("=" * 72)
    print()

    for numero, (base, fichier, est_attaque, nom_c) in enumerate(
        taches,
        start=1,
    ):

        role = "attaque" if est_attaque else "maintien"

        print(
            f"[{numero}/{len(taches)}] {fichier} ({role} -> {nom_c})"
        )

        try:

            table, info = (
                extraire_table_unique(
                    fichier,
                    base,
                    est_attaque,
                )
            )

            tables_brutes.append(
                (
                    nom_c,
                    fichier,
                    table,
                    info,
                )
            )

            print(
                f"    fréquence théorique : "
                f"{info['frequence_theorique']:.6f} Hz"
            )

            print(
                f"    fréquence mesurée   : "
                f"{info['frequence_mesuree']:.6f} Hz"
            )

            print(
                f"    erreur fréquence    : "
                f"{info['erreur_frequence_pct']:+.3f} %"
            )

            print(
                f"    période             : "
                f"{info['periode']:.6f} samples"
            )

            print(
                f"    taille table        : "
                f"{len(table)}"
            )

            print(
                f"    position retenue    : "
                f"{info['position'] / SAMPLE_RATE:.6f} s"
            )

            print(
                f"    score stabilité     : "
                f"{info['score_stable']:.10f}"
            )

            print(
                f"    score raccord       : "
                f"{info['score_raccord']:.10f}"
            )

            print(
                f"    premier échantillon : "
                f"{table[0]:+.6f}"
            )

            print(
                f"    dernier échantillon : "
                f"{table[-1]:+.6f}"
            )

            print()

        except Exception as exc:

            message = (
                f"{fichier}: {exc}"
            )

            erreurs.append(
                message
            )

            print(
                f"    ERREUR : {exc}"
            )

            print()

            if MODE_STRICT:
                raise


    if not tables_brutes:
        raise RuntimeError(
            "Aucune note valide."
        )


    # ------------------------------------------------------------------------
    # NORMALISATION GLOBALE
    # ------------------------------------------------------------------------
    #
    # Très important :
    #
    # On ne normalise PAS chaque note/role individuellement.
    #
    # On trouve le peak global de TOUTES les tables (attaque + maintien) et
    # on applique le même gain à toutes.
    #
    # Cela conserve les différences naturelles entre les prises.
    # ------------------------------------------------------------------------

    if NORMALISATION_GLOBALE:

        normalisees = (
            normaliser_tables_globalement(
                [
                    table
                    for (
                        _nom_c,
                        _fichier,
                        table,
                        _info
                    ) in tables_brutes
                ]
            )
        )

        tables = [
            (
                nom_c,
                fichier,
                convertir_int16(
                    table
                ),
                info,
            )
            for (
                nom_c,
                fichier,
                _ancienne_table,
                info
            ), table in zip(
                tables_brutes,
                normalisees,
            )
        ]

    else:

        tables = [
            (
                nom_c,
                fichier,
                convertir_int16(
                    table
                ),
                info,
            )
            for (
                nom_c,
                fichier,
                table,
                info
            ) in tables_brutes
        ]


    # ------------------------------------------------------------------------
    # EXPORT
    # ------------------------------------------------------------------------

    generer_accordion_tables_h(
        tables,
        fichier_sortie_h,
    )

    generer_accordion_tables_c(
        tables,
        fichier_sortie_c,
    )


    rapport = Path(
        FICHIER_RAPPORT
    )

    generer_rapport_csv(
        tables,
        rapport,
    )


    print("-" * 72)

    print(
        f"Header genere  : "
        f"{fichier_sortie_h}"
    )

    print(
        f"Source generee : "
        f"{fichier_sortie_c}"
    )

    print(
        f"Rapport CSV    : "
        f"{rapport}"
    )

    print(
        f"Nombre tables  : "
        f"{len(tables)} ({len(notes_base)} notes x 2 roles)"
    )

    print()

    print(
        "Traitement spectral : "
        "CONSERVATION DE LA FORME D'ONDE ORIGINALE"
    )

    print(
        "Taille par table : periode naturelle mesuree (pas de taille fixe)"
    )

    print(
        "Band-limiting BL0-BL3 (maintien) / plein spectre (attaque) : "
        "fait au runtime par Wavetable_Init() (wavetable.c)"
    )

    if erreurs:

        print(
            f"Erreurs : {len(erreurs)}"
        )

        for erreur in erreurs:
            print(
                f"  - {erreur}"
            )

    print(
        "-" * 72
    )

    print()

    return tables


# ============================================================================
# MAIN
# ============================================================================

if __name__ == "__main__":

    traiter_toutes_les_notes(
        NOTES_BASE,
        FICHIER_SORTIE_H,
        FICHIER_SORTIE_C,
    )

