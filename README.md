# DVCSUS

Système de gestion des sources expérimental basé sur une base de données

## Prérequis
DVCUS nécessite que les outils/bibliothèques suivants soient présents pour pouvoir être compilé.
* [Boost](https://www.boost.org/) (version minimale: 1.70)
* [CMake](https://cmake.org/) (version minimale: 3.12)

## Dépendances
DVCUS utilise les bibliothèques suivantes:
* [fmt](https://fmt.dev/latest/index.html)
* [sqlite3](https://sqlite.org/index.html)
* [zlib](https://www.zlib.net/)

À noter qu'elles n'ont pas à être installées au préalable. CMake va se charger de les rendre disponibles lors de l'étape de configuration du système de production.

## Compilation

### Linux
Assumant être positionné à la racine des sources de DVCSUS:
```bash
mkdir build
cd build
cmake ..
make
```

### Windows
**TODO**

## Utilisation
```bash
usage: dvcsus <command> [<args>]

These are common dvcsus commands used in various situations:

help             Shows help menu
init             Creates an empty repository or reinitialize an existing one
add              Adds file contents to the staging area
commit           Record changes to the repository
set_remote       Sets the remote repository to pull/push changes from
push             Pushes local changes to the remote repository
pull             Pulls local changes to the remote repository
branch_create    Creates a new branch
branch_checkout  Checks out a given branch

```
