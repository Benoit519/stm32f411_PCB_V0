import re
from pathlib import Path

import numpy as np
import soundfile as sf


# ============================================================
# CONFIGURATION
# ============================================================

# Taux d'échantillonnage attendu du WAV source.
SAMPLE_RATE = 44100

# Nombre de périodes utilisées pour construire la période
# représentative.
NOMBRE_PERIODES_MOYENNE = 4

# Taille minimale / maximale de la période finale.
MIN_TABLE_SIZE = 40
MAX_TABLE_SIZE = 4096

# Amplitude maximale de sortie.
INT16_MAX = 32767
AMPLITUDE = 0.95

# ------------------------------------------------------------
# Détection de fréquence
# ------------------------------------------------------------

# True :
#   mesure la fréquence réelle du fichier.
#
# False :
#   utilise uniquement la fréquence donnée par le nom.
#
# Pour un accordéon réel, True est recommandé.
UTILISER_FREQUENCE_REELLE = True

# Tolérance autour de la fréquence théorique.
# Exemple :
#   Do3 théorique = 130.81 Hz
#   recherche entre 0.95 et 1.05 fois cette fréquence.
FREQUENCE_MIN_RATIO = 0.90
FREQUENCE_MAX_RATIO = 1.10

# ------------------------------------------------------------
# Recherche de zone stable
# ------------------------------------------------------------

DEBUT_RECHERCHE_SECONDES = 0.10

# ------------------------------------------------------------
# Alignement de phase
# ------------------------------------------------------------

# Recherche de correction de phase autour de chaque période.
ALIGNEMENT_PHASE = True

# Nombre de points utilisés pour calculer la corrélation
# autour du début de période.
ALIGNEMENT_ZONE = 64

# ------------------------------------------------------------
# Optimisation du raccord final
# ------------------------------------------------------------

RACCORD_ZONE_MAX = 32

# Nombre de positions testées autour de la position finale.
RACCORD_RECHERCHE = 20

# ------------------------------------------------------------
# Export
# ------------------------------------------------------------

EXPORT_INT16 = True


# ============================================================
# NOTES
# ============================================================

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


# ============================================================
# FREQUENCE DEPUIS LE NOM DU FICHIER
# ============================================================

def frequence_depuis_nom(fichier):
    """
    Exemples :

        la2.wav
        do3.wav
        fa#3.wav
        sib1.wav
    """

    nom = Path(fichier).stem.lower()

    match = re.search(
        r"([a-z]+[#b]?)(-?\d+)",
        nom
    )

    if not match:
        raise ValueError(
            f"Impossible de trouver la note dans : {nom}"
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
        440.0
        * 2 ** ((midi - 69) / 12)
    )


# ============================================================
# EXTRACTION INTERPOLEE
# ============================================================

def extraire_periode_interpolee(
    signal,
    position,
    periode,
    taille
):
    """
    Extrait une période sans dupliquer le premier échantillon
    à la fin.

    Les positions couvrent :

        position
        ...
        position + periode * (taille - 1) / taille

    Le point :

        position + periode

    est implicitement équivalent au début de la période.

    IMPORTANT :
    L'interpolation est cyclique uniquement dans la wavetable
    finale. Ici, on travaille sur le signal réel.
    """

    positions = (
        position
        + np.arange(taille, dtype=np.float64)
        * periode
        / taille
    )

    indices = np.arange(
        len(signal),
        dtype=np.float64
    )

    return np.interp(
        positions,
        indices,
        signal
    )


# ============================================================
# EXTRACTION DE PLUSIEURS PERIODES
# ============================================================

def extraire_plusieurs_periodes(
    signal,
    position,
    periode,
    nombre_periodes,
    taille
):
    """
    Retourne :

        [nombre_periodes, taille]
    """

    periodes = []

    for p in range(nombre_periodes):

        position_p = (
            position
            + p * periode
        )

        cycle = extraire_periode_interpolee(
            signal,
            position_p,
            periode,
            taille
        )

        periodes.append(cycle)

    return np.asarray(
        periodes,
        dtype=np.float64
    )


# ============================================================
# AUTOCORRELATION
# ============================================================

def estimer_frequence_reelle(
    signal,
    frequence_theorique,
    sr
):
    """
    Estime la fréquence fondamentale autour de la fréquence
    théorique donnée par le nom du fichier.

    Utilise l'autocorrélation.

    La recherche est limitée à une plage autour de la
    fréquence théorique afin d'éviter de choisir une
    harmonique.
    """

    signal = np.asarray(
        signal,
        dtype=np.float64
    )

    # --------------------------------------------------------
    # On utilise une portion située après l'attaque.
    # --------------------------------------------------------

    debut = int(
        0.20 * sr
    )

    duree = int(
        0.25 * sr
    )

    fin = min(
        len(signal),
        debut + duree
    )

    if fin - debut < sr * 0.05:
        raise ValueError(
            "Signal trop court pour mesurer "
            "la fréquence fondamentale."
        )

    x = signal[
        debut:fin
    ].copy()

    # --------------------------------------------------------
    # Suppression DC
    # --------------------------------------------------------

    x -= np.mean(x)

    # --------------------------------------------------------
    # Fenêtre Hann
    # --------------------------------------------------------

    x *= np.hanning(
        len(x)
    )

    energie = np.sum(
        x * x
    )

    if energie < 1e-12:
        raise ValueError(
            "Signal trop faible pour mesurer "
            "la fréquence."
        )

    # --------------------------------------------------------
    # Périodes recherchées
    # --------------------------------------------------------

    f_min = (
        frequence_theorique
        * FREQUENCE_MIN_RATIO
    )

    f_max = (
        frequence_theorique
        * FREQUENCE_MAX_RATIO
    )

    periode_min = int(
        np.floor(sr / f_max)
    )

    periode_max = int(
        np.ceil(sr / f_min)
    )

    if periode_min < 2:
        periode_min = 2

    # --------------------------------------------------------
    # Autocorrélation
    # --------------------------------------------------------

    correlation = np.correlate(
        x,
        x,
        mode="full"
    )

    correlation = correlation[
        len(x) - 1:
    ]

    correlation /= (
        energie + 1e-20
    )

    periode_max = min(
        periode_max,
        len(correlation) - 1
    )

    if periode_min >= periode_max:
        raise ValueError(
            "Plage de recherche de fréquence invalide."
        )

    zone = correlation[
        periode_min:
        periode_max + 1
    ]

    index_local = int(
        np.argmax(zone)
    )

    lag = (
        periode_min
        + index_local
    )

    # --------------------------------------------------------
    # Interpolation parabolique autour du maximum
    # --------------------------------------------------------

    if (
        lag > 0
        and lag < len(correlation) - 1
    ):

        y1 = correlation[lag - 1]
        y2 = correlation[lag]
        y3 = correlation[lag + 1]

        denom = (
            y1
            - 2.0 * y2
            + y3
        )

        if abs(denom) > 1e-12:

            delta = (
                0.5
                * (y1 - y3)
                / denom
            )

        else:

            delta = 0.0

    else:

        delta = 0.0

    periode_reelle = (
        lag + delta
    )

    if periode_reelle <= 0:
        raise ValueError(
            "Impossible d'estimer la période."
        )

    frequence_reelle = (
        sr / periode_reelle
    )

    return frequence_reelle


# ============================================================
# RECHERCHE D'UNE ZONE STABLE
# ============================================================

def trouver_zone_stable(
    signal,
    frequence,
    sr,
    nombre_periodes
):
    """
    Recherche une zone où plusieurs périodes consécutives
    se ressemblent le plus.
    """

    periode = (
        sr / frequence
    )

    taille_analyse = max(
        MIN_TABLE_SIZE,
        int(round(periode))
    )

    if taille_analyse > MAX_TABLE_SIZE:
        raise ValueError(
            f"Période trop longue : "
            f"{taille_analyse} échantillons."
        )

    debut_recherche = int(
        DEBUT_RECHERCHE_SECONDES * sr
    )

    longueur_recherche = (
        nombre_periodes * periode
    )

    fin_recherche = int(
        len(signal)
        - longueur_recherche
        - 2
    )

    if fin_recherche <= debut_recherche:
        raise ValueError(
            "Le fichier est trop court pour extraire "
            f"{nombre_periodes} périodes."
        )

    pas = max(
        1,
        int(round(periode / 4.0))
    )

    meilleur_score = np.inf
    meilleure_position = float(
        debut_recherche
    )

    for position in range(
        debut_recherche,
        fin_recherche,
        pas
    ):

        periodes = extraire_plusieurs_periodes(
            signal,
            float(position),
            periode,
            nombre_periodes,
            taille_analyse
        )

        moyenne = np.mean(
            periodes,
            axis=0
        )

        erreur = np.mean(
            (periodes - moyenne) ** 2
        )

        energie = np.mean(
            moyenne ** 2
        )

        if energie > 1e-12:
            erreur /= energie

        if erreur < meilleur_score:

            meilleur_score = erreur

            meilleure_position = float(
                position
            )

    return (
        meilleure_position,
        meilleur_score
    )


# ============================================================
# ALIGNEMENT DE PHASE
# ============================================================

def aligner_periode_sur_reference(
    periode,
    reference,
    recherche=ALIGNEMENT_ZONE
):
    """
    Aligne une période sur une référence par recherche du
    meilleur décalage circulaire.

    Cela évite de moyenner des périodes légèrement décalées
    en phase.
    """

    n = len(periode)

    if n < 4:
        return periode.copy()

    zone = min(
        recherche,
        n // 4
    )

    if zone < 4:
        return periode.copy()

    # --------------------------------------------------------
    # On compare principalement une zone autour du début.
    # --------------------------------------------------------

    ref = reference[
        :zone
    ]

    meilleur_score = np.inf
    meilleur_shift = 0

    for shift in range(
        -zone,
        zone + 1
    ):

        candidate = np.roll(
            periode,
            shift
        )

        c = candidate[
            :zone
        ]

        score = np.mean(
            (c - ref) ** 2
        )

        if score < meilleur_score:

            meilleur_score = score
            meilleur_shift = shift

    return np.roll(
        periode,
        meilleur_shift
    )


# ============================================================
# MOYENNE AVEC ALIGNEMENT
# ============================================================

def construire_periode_moyenne(
    signal,
    position,
    frequence,
    sr,
    nombre_periodes
):
    """
    Extrait plusieurs périodes, les aligne en phase,
    puis les moyenne.
    """

    periode = (
        sr / frequence
    )

    taille = int(
        round(periode)
    )

    if taille < MIN_TABLE_SIZE:
        raise ValueError(
            f"Période trop courte : {taille}"
        )

    if taille > MAX_TABLE_SIZE:
        raise ValueError(
            f"Période trop longue : {taille}"
        )

    periodes = extraire_plusieurs_periodes(
        signal,
        position,
        periode,
        nombre_periodes,
        taille
    )

    # --------------------------------------------------------
    # Référence initiale
    # --------------------------------------------------------

    reference = periodes[0].copy()

    periodes_alignees = []

    for p in periodes:

        if ALIGNEMENT_PHASE:

            p_alignee = (
                aligner_periode_sur_reference(
                    p,
                    reference
                )
            )

        else:

            p_alignee = p.copy()

        periodes_alignees.append(
            p_alignee
        )

    periodes_alignees = np.asarray(
        periodes_alignees,
        dtype=np.float64
    )

    periode_moyenne = np.mean(
        periodes_alignees,
        axis=0
    )

    return (
        periode_moyenne,
        periodes_alignees
    )


# ============================================================
# OPTIMISATION DU RACCORDEMENT FINAL
# ============================================================

def score_raccordement(
    periode
):
    """
    Mesure la qualité du raccord périodique.

    On compare les zones de début et de fin.

    Le score tient compte :

        1. amplitude
        2. pente
        3. pente directement au raccord
    """

    n = len(periode)

    zone = min(
        RACCORD_ZONE_MAX,
        max(8, n // 10)
    )

    if n < 2 * zone + 2:
        return np.inf

    debut = periode[
        :zone
    ]

    fin = periode[
        -zone:
    ]

    # --------------------------------------------------------
    # Différence de forme
    # --------------------------------------------------------

    erreur_amplitude = np.mean(
        (debut - fin) ** 2
    )

    # --------------------------------------------------------
    # Pentes locales
    # --------------------------------------------------------

    pente_debut = np.diff(
        debut
    )

    pente_fin = np.diff(
        fin
    )

    erreur_pente = np.mean(
        (pente_debut - pente_fin) ** 2
    )

    # --------------------------------------------------------
    # Pente exacte du raccord circulaire
    # --------------------------------------------------------

    pente_raccord = (
        periode[0]
        - periode[-1]
    )

    pente_avant = (
        periode[-1]
        - periode[-2]
    )

    pente_apres = (
        periode[1]
        - periode[0]
    )

    erreur_pente_raccord = (
        (pente_raccord - pente_avant) ** 2
        +
        (pente_raccord - pente_apres) ** 2
    )

    # --------------------------------------------------------
    # Pondération
    # --------------------------------------------------------

    return (
        erreur_amplitude
        + 0.5 * erreur_pente
        + 2.0 * erreur_pente_raccord
    )


def optimiser_raccordement_final(
    signal,
    position,
    frequence,
    sr,
    nombre_periodes,
    recherche=RACCORD_RECHERCHE
):
    """
    Optimise directement le raccord de la période MOYENNE
    finale.

    C'est volontairement fait APRÈS la moyenne.
    """

    periode = (
        sr / frequence
    )

    longueur = int(
        round(periode)
    )

    positions = np.linspace(
        position - recherche,
        position + recherche,
        recherche * 8 + 1
    )

    meilleure_position = position
    meilleur_score = np.inf

    for pos in positions:

        moyenne, _ = construire_periode_moyenne(
            signal,
            pos,
            frequence,
            sr,
            nombre_periodes
        )

        score = score_raccordement(
            moyenne
        )

        if score < meilleur_score:

            meilleur_score = score
            meilleure_position = pos

    return (
        meilleure_position,
        meilleur_score
    )


# ============================================================
# CORRECTION LEGERE DU RACCORDEMENT
# ============================================================

def corriger_raccordement_doucement(
    wavetable
):
    """
    Applique une correction douce aux extrémités afin de
    réduire une éventuelle discontinuité.

    IMPORTANT :

    On ne force PAS :

        dernier = premier

    On distribue progressivement la correction sur une zone
    afin d'éviter une cassure locale.
    """

    x = wavetable.copy()

    n = len(x)

    zone = min(
        16,
        max(4, n // 16)
    )

    if n < 2 * zone + 2:
        return x

    # --------------------------------------------------------
    # Différence au raccord.
    # --------------------------------------------------------

    difference = (
        x[0]
        - x[-1]
    )

    if abs(difference) < 1e-12:
        return x

    # --------------------------------------------------------
    # Correction progressive.
    #
    # Début : correction négative
    # Fin   : correction positive
    #
    # La correction est répartie sur plusieurs points.
    # --------------------------------------------------------

    t = np.linspace(
        0.0,
        1.0,
        zone
    )

    fenetre = (
        0.5
        - 0.5 * np.cos(
            np.pi * t
        )
    )

    # Début
    x[:zone] -= (
        difference
        * 0.5
        * (1.0 - fenetre)
    )

    # Fin
    x[-zone:] += (
        difference
        * 0.5
        * fenetre
    )

    return x


# ============================================================
# CONVERSION INT16
# ============================================================

def convertir_int16(
    signal
):
    """
    Float [-1,+1] -> int16.
    """

    signal = np.clip(
        signal,
        -1.0,
        1.0
    )

    return np.round(
        signal * INT16_MAX
    ).astype(
        np.int16
    )


# ============================================================
# NOM C VALIDE
# ============================================================

def nom_c_valide(
    fichier
):
    nom = Path(
        fichier
    ).stem

    nom = re.sub(
        r"[^a-zA-Z0-9_]",
        "_",
        nom
    )

    if not nom:
        raise ValueError(
            "Nom de fichier invalide."
        )

    if nom[0].isdigit():
        nom = "_" + nom

    return nom


# ============================================================
# GENERATION FICHIER C
# ============================================================

def generer_fichier_c(
    fichier_entree,
    signal_int16,
    fichier_sortie=None
):
    """
    Génère par exemple :

        #define DO3_SIZE 734

        const int16_t do3[DO3_SIZE] =
        {
            ...
        };
    """

    nom = nom_c_valide(
        fichier_entree
    )

    nom_macro = nom.upper()

    if fichier_sortie is None:

        fichier_sortie = (
            Path(fichier_entree).parent
            / f"{nom}.h"
        )

    taille = len(
        signal_int16
    )

    lignes = []

    lignes.append(
        "#ifndef GENERATED_WAVETABLE_H"
    )

    lignes.append(
        "#define GENERATED_WAVETABLE_H"
    )

    lignes.append("")

    lignes.append(
        "#include <stdint.h>"
    )

    lignes.append("")

    lignes.append(
        f"#define {nom_macro}_SIZE {taille}"
    )

    lignes.append("")

    lignes.append(
        f"const int16_t {nom}[{nom_macro}_SIZE] ="
    )

    lignes.append("{")

    valeurs_par_ligne = 10

    for i in range(
        0,
        taille,
        valeurs_par_ligne
    ):

        bloc = signal_int16[
            i:
            i + valeurs_par_ligne
        ]

        texte = ",".join(
            str(int(x))
            for x in bloc
        )

        lignes.append(
            f"    {texte},"
        )

    if len(lignes) > 2:
        lignes[-1] = (
            lignes[-1].rstrip(",")
        )

    lignes.append(
        "};"
    )

    lignes.append("")

    lignes.append(
        "#endif"
    )

    lignes.append("")

    with open(
        fichier_sortie,
        "w",
        encoding="utf-8"
    ) as f:

        f.write(
            "\n".join(lignes)
        )

    return fichier_sortie


# ============================================================
# ANALYSE DU RACCORDEMENT
# ============================================================

def analyser_raccord(
    signal
):
    """
    Retourne plusieurs mesures du raccord.
    """

    if len(signal) < 3:
        return {
            "difference_amplitude": 0.0,
            "pente_raccord": 0.0,
            "pente_avant": 0.0,
            "pente_apres": 0.0,
        }

    difference = (
        signal[0]
        - signal[-1]
    )

    pente_raccord = difference

    pente_avant = (
        signal[-1]
        - signal[-2]
    )

    pente_apres = (
        signal[1]
        - signal[0]
    )

    return {
        "difference_amplitude": float(
            difference
        ),
        "pente_raccord": float(
            pente_raccord
        ),
        "pente_avant": float(
            pente_avant
        ),
        "pente_apres": float(
            pente_apres
        ),
    }


# ============================================================
# TRAITEMENT PRINCIPAL
# ============================================================

def extraire_wavetable(
    fichier_entree,
    fichier_sortie=None
):

    # ========================================================
    # LECTURE
    # ========================================================

    signal, sr = sf.read(
        fichier_entree,
        always_2d=False
    )

    print()
    print("==========================================")
    print("       WAV -> WAVETABLE C")
    print("==========================================")
    print()

    print(
        f"Fichier       : {fichier_entree}"
    )

    print(
        f"Sample rate   : {sr} Hz"
    )

    # ========================================================
    # SAMPLE RATE
    # ========================================================

    if sr != SAMPLE_RATE:

        raise ValueError(
            f"Le fichier doit être à "
            f"{SAMPLE_RATE} Hz."
        )

    # ========================================================
    # STEREO -> MONO
    # ========================================================

    if signal.ndim == 2:

        print(
            "Conversion    : stéréo -> mono"
        )

        signal = np.mean(
            signal,
            axis=1
        )

    # ========================================================
    # FLOAT64
    # ========================================================

    signal = signal.astype(
        np.float64
    )

    # ========================================================
    # DC
    # ========================================================

    signal -= np.mean(
        signal
    )

    # ========================================================
    # FREQUENCE THEORIQUE
    # ========================================================

    frequence_theorique = (
        frequence_depuis_nom(
            fichier_entree
        )
    )

    print(
        f"Fréquence nom : "
        f"{frequence_theorique:.9f} Hz"
    )

    # ========================================================
    # FREQUENCE REELLE
    # ========================================================

    if UTILISER_FREQUENCE_REELLE:

        frequence = (
            estimer_frequence_reelle(
                signal,
                frequence_theorique,
                sr
            )
        )

        print(
            f"Fréquence mes : "
            f"{frequence:.9f} Hz"
        )

    else:

        frequence = (
            frequence_theorique
        )

    # ========================================================
    # PERIODE
    # ========================================================

    periode = (
        sr / frequence
    )

    longueur = int(
        round(periode)
    )

    print(
        f"Période        : "
        f"{periode:.9f} échantillons"
    )

    print(
        f"Taille période : "
        f"{longueur} échantillons"
    )

    print(
        f"Moyenne        : "
        f"{NOMBRE_PERIODES_MOYENNE} périodes"
    )

    # ========================================================
    # VERIFICATION
    # ========================================================

    if longueur < MIN_TABLE_SIZE:

        raise ValueError(
            f"Période trop courte : "
            f"{longueur}"
        )

    if longueur > MAX_TABLE_SIZE:

        raise ValueError(
            f"Période trop longue : "
            f"{longueur}"
        )

    # ========================================================
    # ZONE STABLE
    # ========================================================

    position, score_stable = (
        trouver_zone_stable(
            signal,
            frequence,
            sr,
            NOMBRE_PERIODES_MOYENNE
        )
    )

    print()

    print(
        f"Position stable : "
        f"{position / sr:.6f} s"
    )

    print(
        f"Score stabilité : "
        f"{score_stable:.10f}"
    )

    # ========================================================
    # PREMIERE CONSTRUCTION
    # ========================================================

    wavetable, periodes = (
        construire_periode_moyenne(
            signal,
            position,
            frequence,
            sr,
            NOMBRE_PERIODES_MOYENNE
        )
    )

    # ========================================================
    # OPTIMISATION DU RACCORDEMENT FINAL
    # ========================================================

    position_optimisee, score_raccord = (
        optimiser_raccordement_final(
            signal,
            position,
            frequence,
            sr,
            NOMBRE_PERIODES_MOYENNE
        )
    )

    print(
        f"Position raccord: "
        f"{position_optimisee / sr:.6f} s"
    )

    print(
        f"Score raccord   : "
        f"{score_raccord:.10f}"
    )

    # ========================================================
    # RECONSTRUCTION A LA POSITION OPTIMISEE
    # ========================================================

    wavetable, periodes = (
        construire_periode_moyenne(
            signal,
            position_optimisee,
            frequence,
            sr,
            NOMBRE_PERIODES_MOYENNE
        )
    )

    # ========================================================
    # DC RESIDUEL
    # ========================================================

    wavetable -= np.mean(
        wavetable
    )

    # ========================================================
    # CORRECTION DOUCE DU RACCORDEMENT
    # ========================================================

    wavetable = (
        corriger_raccordement_doucement(
            wavetable
        )
    )

    # ========================================================
    # DC APRES CORRECTION
    # ========================================================

    wavetable -= np.mean(
        wavetable
    )

    # ========================================================
    # NORMALISATION
    # ========================================================

    amplitude = np.max(
        np.abs(wavetable)
    )

    if amplitude > 1e-12:

        wavetable /= amplitude

        wavetable *= AMPLITUDE

    # ========================================================
    # INT16
    # ========================================================

    wavetable_int16 = (
        convertir_int16(
            wavetable
        )
    )

    # ========================================================
    # EXPORT C
    # ========================================================

    fichier = generer_fichier_c(
        fichier_entree,
        wavetable_int16,
        fichier_sortie
    )

    # ========================================================
    # ANALYSE DU RACCORDEMENT
    # ========================================================

    raccord = analyser_raccord(
        wavetable_int16.astype(
            np.float64
        )
    )

    # ========================================================
    # FREQUENCE DE LA TABLE
    # ========================================================

    frequence_table = (
        sr / longueur
    )

    erreur_frequence = (
        frequence_table
        - frequence
    )

    # ========================================================
    # RESULTATS
    # ========================================================

    print()
    print("------------------------------------------")
    print("Résultat")
    print("------------------------------------------")

    print(
        f"Fichier C          : {fichier}"
    )

    print(
        f"Sample rate source : {sr} Hz"
    )

    print(
        f"Fréquence théorique: "
        f"{frequence_theorique:.9f} Hz"
    )

    print(
        f"Fréquence mesurée  : "
        f"{frequence:.9f} Hz"
    )

    print(
        f"Période exacte     : "
        f"{periode:.9f}"
    )

    print(
        f"Taille table       : "
        f"{longueur}"
    )

    print(
        f"Fréquence table    : "
        f"{frequence_table:.9f} Hz"
    )

    print(
        f"Erreur fréquence   : "
        f"{erreur_frequence:+.9f} Hz"
    )

    print()

    print(
        f"Premier            : "
        f"{int(wavetable_int16[0])}"
    )

    print(
        f"Dernier            : "
        f"{int(wavetable_int16[-1])}"
    )

    print(
        f"Différence         : "
        f"{raccord['difference_amplitude']:+.3f}"
    )

    print(
        f"Pente raccord      : "
        f"{raccord['pente_raccord']:+.3f}"
    )

    print(
        f"Pente avant        : "
        f"{raccord['pente_avant']:+.3f}"
    )

    print(
        f"Pente après        : "
        f"{raccord['pente_apres']:+.3f}"
    )

    print("------------------------------------------")
    print()

    return wavetable_int16


# ============================================================
# MAIN
# ============================================================

if __name__ == "__main__":

    extraire_wavetable(
        "sol5.wav"
    )