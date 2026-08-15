import re
from pathlib import Path

import numpy as np
import soundfile as sf


# ============================================================
# CONFIGURATION
# ============================================================

SAMPLE_RATE = 44100

# Nombre minimum / maximum de périodes testées
MIN_PERIODES = 1
MAX_PERIODES = 4

# Taille minimale / maximale de la wavetable
MIN_TABLE_SIZE = 64
MAX_TABLE_SIZE = 4096

# Niveau maximal du int16
INT16_MAX = 32767

# Marge pour éviter exactement +32767 / -32768
AMPLITUDE = 0.95


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
    Convention utilisée :

        la1 = 220 Hz
        la2 = 440 Hz
        la3 = 880 Hz
        la4 = 1760 Hz

    Exemples :

        la2.wav
        do2.wav
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

    # Numéro MIDI standard
    midi = (
        12 * (octave + 1)
        + NOTE_SEMITONES[note]
    )

    frequence = (
        440.0
        * 2 ** ((midi - 69) / 12)
    )

    return frequence


# ============================================================
# RECHERCHE DU NOMBRE DE PERIODES
# ============================================================

def trouver_meilleure_table(
    frequence,
    sr
):
    """
    Cherche automatiquement un nombre de périodes donnant
    une longueur entière d'échantillons.

    On minimise l'erreur entre :

        N * période

    et une longueur entière.
    """

    periode = sr / frequence

    candidats = []

    for n in range(
        MIN_PERIODES,
        MAX_PERIODES + 1
    ):

        longueur_exacte = (
            n * periode
        )

        longueur = round(
            longueur_exacte
        )

        if longueur < MIN_TABLE_SIZE:
            continue

        if longueur > MAX_TABLE_SIZE:
            continue

        erreur_echantillons = abs(
            longueur_exacte - longueur
        )

        # Fréquence réellement représentée
        frequence_reelle = (
            n * sr / longueur
        )

        erreur_frequence = abs(
            frequence_reelle - frequence
        )

        candidats.append(
            (
                erreur_echantillons,
                erreur_frequence,
                n,
                longueur,
                frequence_reelle
            )
        )

    if not candidats:
        raise ValueError(
            "Impossible de trouver une taille "
            "de wavetable valide."
        )

    # Priorité à la précision de la longueur.
    candidats.sort(
        key=lambda x: (
            x[0],
            x[1]
        )
    )

    return candidats[0]


# ============================================================
# EXTRACTION D'UNE PORTION STABLE
# ============================================================

def trouver_zone_stable(
    signal,
    frequence,
    sr,
    longueur
):
    """
    Cherche une zone du fichier où le signal est suffisamment
    stable et périodique.

    On compare la portion candidate avec la même portion
    décalée d'environ une période.
    """

    periode = sr / frequence

    n_periode = int(
        round(periode)
    )

    # On ignore l'attaque.
    debut_recherche = int(
        0.1 * sr
    )

    # On ne peut pas dépasser la fin.
    fin_recherche = (
        len(signal)
        - longueur
        - n_periode
    )

    if fin_recherche <= debut_recherche:
        raise ValueError(
            "Le fichier est trop court."
        )

    meilleur_score = np.inf
    meilleur_position = debut_recherche

    # Recherche assez fine
    pas = max(
        1,
        n_periode // 4
    )

    for position in range(
        debut_recherche,
        fin_recherche,
        pas
    ):

        a = signal[
            position:
            position + longueur
        ]

        b = signal[
            position + n_periode:
            position + n_periode + longueur
        ]

        if len(a) != longueur:
            continue

        if len(b) != longueur:
            continue

        # Erreur normalisée
        erreur = np.mean(
            (a - b) ** 2
        )

        energie = np.mean(
            a ** 2
        )

        if energie > 1e-12:
            erreur /= energie

        if erreur < meilleur_score:
            meilleur_score = erreur
            meilleur_position = position

    return meilleur_position, meilleur_score


# ============================================================
# OPTIMISATION DU RACCORDEMENT
# ============================================================

def optimiser_raccordement(
    signal,
    position,
    longueur,
    recherche=20
):
    """
    Cherche un léger déplacement de la position de départ
    afin de minimiser la différence entre le début et la fin
    de la table.

    On compare :

        signal[position]

    avec :

        signal[position + longueur]

    ainsi que les échantillons autour.
    """

    meilleur_position = position
    meilleur_score = np.inf

    debut = max(
        0,
        position - recherche
    )

    fin = min(
        len(signal) - longueur - 1,
        position + recherche
    )

    # Taille de la zone de comparaison
    zone = min(
        32,
        longueur // 10
    )

    for pos in range(
        debut,
        fin + 1
    ):

        a = signal[
            pos:
            pos + zone
        ]

        b = signal[
            pos + longueur:
            pos + longueur + zone
        ]

        if len(a) != zone:
            continue

        if len(b) != zone:
            continue

        # Erreur de raccordement
        erreur_amplitude = np.mean(
            (a - b) ** 2
        )

        # Comparaison également des pentes
        da = np.diff(a)
        db = np.diff(b)

        erreur_pente = np.mean(
            (da - db) ** 2
        )

        score = (
            erreur_amplitude
            + 0.5 * erreur_pente
        )

        if score < meilleur_score:
            meilleur_score = score
            meilleur_position = pos

    return (
        meilleur_position,
        meilleur_score
    )


# ============================================================
# CONVERSION EN INT16
# ============================================================

def convertir_int16(signal):
    """
    Convertit le signal float [-1, +1] en int16.
    """

    signal = np.clip(
        signal,
        -1.0,
        1.0
    )

    return np.round(
        signal * INT16_MAX
    ).astype(np.int16)


# ============================================================
# NOM VALIDE POUR LE C
# ============================================================

def nom_c_valide(fichier):
    """
    la3.wav -> la3
    Casse conservée pour le nom du tableau,
    et version majuscule pour LA3_SIZE.
    """

    nom = Path(fichier).stem

    # Remplace les caractères non valides
    nom = re.sub(
        r"[^a-zA-Z0-9_]",
        "_",
        nom
    )

    if nom[0].isdigit():
        nom = "_" + nom

    return nom


# ============================================================
# GENERATION DU FICHIER C
# ============================================================

def generer_fichier_c(
    fichier_entree,
    signal_int16,
    fichier_sortie=None
):
    """
    Génère :

        const int16_t la3[LA3_SIZE] =
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

    # --------------------------------------------------------
    # Construction du texte
    # --------------------------------------------------------

    lignes = []

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

    # 10 valeurs par ligne
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
            f"{texte},"
        )

    # Supprimer la dernière virgule
    if len(lignes) > 2:
        lignes[-1] = (
            lignes[-1].rstrip(",")
        )

    lignes.append("};")
    lignes.append("")

    # --------------------------------------------------------
    # Écriture
    # --------------------------------------------------------

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
# TRAITEMENT PRINCIPAL
# ============================================================

def extraire_wavetable(
    fichier_entree,
    fichier_sortie=None
):
    """
    Transforme :

        la3.wav

    en :

        la3.h

    contenant un tableau int16_t utilisable en C/C++.
    """

    # --------------------------------------------------------
    # Lecture
    # --------------------------------------------------------

    signal, sr = sf.read(
        fichier_entree
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

    # --------------------------------------------------------
    # Vérification
    # --------------------------------------------------------

    if sr != SAMPLE_RATE:
        raise ValueError(
            f"Le fichier doit être à "
            f"{SAMPLE_RATE} Hz."
        )

    # --------------------------------------------------------
    # Stéréo -> mono
    # --------------------------------------------------------

    if signal.ndim == 2:

        print(
            "Conversion    : stéréo -> mono"
        )

        signal = np.mean(
            signal,
            axis=1
        )

    # --------------------------------------------------------
    # Float64
    # --------------------------------------------------------

    signal = signal.astype(
        np.float64
    )

    # --------------------------------------------------------
    # Suppression DC
    # --------------------------------------------------------

    signal -= np.mean(signal)

    # --------------------------------------------------------
    # Fréquence
    # --------------------------------------------------------

    frequence = frequence_depuis_nom(
        fichier_entree
    )

    print(
        f"Fréquence     : "
        f"{frequence:.9f} Hz"
    )

    periode = (
        SAMPLE_RATE / frequence
    )

    print(
        f"Période       : "
        f"{periode:.9f} échantillons"
    )

    # --------------------------------------------------------
    # Meilleure longueur
    # --------------------------------------------------------

    (
        erreur_echantillons,
        erreur_frequence,
        nombre_periodes,
        longueur,
        frequence_reelle
    ) = trouver_meilleure_table(
        frequence,
        sr
    )

    print()
    print(
        f"Périodes      : "
        f"{nombre_periodes}"
    )

    print(
        f"Taille table  : "
        f"{longueur} échantillons"
    )

    print(
        f"Fréquence table : "
        f"{frequence_reelle:.9f} Hz"
    )

    print(
        f"Erreur longueur : "
        f"{erreur_echantillons:.9f}"
    )

    # --------------------------------------------------------
    # Recherche de la meilleure zone
    # --------------------------------------------------------

    position, score = trouver_zone_stable(
        signal,
        frequence,
        sr,
        longueur
    )

    print()
    print(
        f"Position initiale : "
        f"{position / sr:.4f} s"
    )

    print(
        f"Score périodique  : "
        f"{score:.8f}"
    )

    # --------------------------------------------------------
    # Optimisation du raccordement
    # --------------------------------------------------------

    position, score_raccord = (
        optimiser_raccordement(
            signal,
            position,
            longueur
        )
    )

    print(
        f"Position optimisée: "
        f"{position / sr:.4f} s"
    )

    print(
        f"Score raccordement : "
        f"{score_raccord:.8f}"
    )

    # --------------------------------------------------------
    # Extraction
    # --------------------------------------------------------

    wavetable = signal[
        position:
        position + longueur
    ].copy()

    if len(wavetable) != longueur:
        raise ValueError(
            "Impossible d'extraire la table complète."
        )

    # --------------------------------------------------------
    # Ajustement du niveau
    # --------------------------------------------------------

    amplitude = np.max(
        np.abs(wavetable)
    )

    if amplitude > 0:

        wavetable /= amplitude

        wavetable *= AMPLITUDE

    # --------------------------------------------------------
    # Conversion int16
    # --------------------------------------------------------

    wavetable_int16 = convertir_int16(
        wavetable
    )

    # --------------------------------------------------------
    # Génération du fichier C
    # --------------------------------------------------------

    fichier = generer_fichier_c(
        fichier_entree,
        wavetable_int16,
        fichier_sortie
    )

    # --------------------------------------------------------
    # Vérification du raccord
    # --------------------------------------------------------

    premier = int(
        wavetable_int16[0]
    )

    dernier = int(
        wavetable_int16[-1]
    )

    print()
    print("------------------------------------------")
    print("Résultat")
    print("------------------------------------------")
    print(
        f"Fichier C     : {fichier}"
    )
    print(
        f"Taille        : {longueur}"
    )
    print(
        f"Premier       : {premier}"
    )
    print(
        f"Dernier       : {dernier}"
    )
    print(
        f"Différence    : {dernier - premier}"
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