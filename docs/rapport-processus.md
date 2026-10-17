# Rapport : validation des processus HekaOS

> Points 3, 4, 9, 10, 11 et 12 du cahier des charges. État au commit `bdc18f0`, 17 octobre 2026.

## Contexte

- **Portée** : extrait du rapport de validation du cahier des charges « processus ». Les autres points (1, 2, 5 à 8, 13 à 15) ne sont pas repris ici.
- **Méthode** : lecture complète du code, puis compilation hors dépôt et exécution sous QEMU 10.2. Les scénarios ont été pilotés via le moniteur QEMU.
- **Légende** : **[exécution]** = observé dans QEMU ; **[lecture]** = déduit du code uniquement.
- Les numéros de ligne correspondent au commit `bdc18f0`.

## Synthèse

| # | Exigence | Statut | Problème principal | Gravité | Effort |
|---|---|---|---|---|---|
| 3 | Pointers to father and children | Conforme | kthreads zombies jamais récoltés | moyenne | moyen |
| 4 | Stack and heap of a process | **Partiel** | pas de tas utilisateur | manque fonctionnel | moyen |
| 9 | Functions to work on the memory of a process | **Partiel** | rien pour faire évoluer la mémoire d'un processus vivant | manque fonctionnel | moyen à gros |
| 10 | fork | Conforme | fuite mémoire si la copie échoue | faible | une ligne |
| 11 | wait | Conforme | sections critiques avec interruptions actives, pointeur non validé | haute | petit |
| 12 | exit | Conforme | tuer le PID 1 bloque le noyau | **critique** | petit |

---

## Point 3 : Pointers to father and children

**Exigence** : « Pointers to father and children »

**Statut** : conforme. Les pointeurs `parent`, `children` et `sibling` existent. Ils sont maintenus (ajout, retrait, rattachement à init) et utilisés par `wait` [exécution]. Problème : les kthreads tués ne sont jamais récoltés.

### Constat [exécution]

Après `kill 7 9` et `kill 63 9`, les kthreads 7 et 63 restent `ZOMBIE` indéfiniment dans eyeproc. Les processus ring 3 9 et 10, tués de la même façon, sont bien récoltés par init.

### Cause [lecture]

1. `exec_fn` rattache chaque kthread à `kthreadd_task` (`src/task.c:284`).
2. kthreadd (PID 2) est une sentinelle statique : il n'entre jamais dans la liste de l'ordonnanceur et n'exécute jamais de code (`src/task.c:36`).
3. À la mort d'un kthread, `do_exit` le passe en `ZOMBIE` et envoie `SIGCHLD` à son parent (`src/task.c:441`), donc à la sentinelle.
4. La seule récolte possible est `task_reap`, appelée uniquement depuis le `sys_wait` du parent (`src/cpu/syscall.c:193`). Or kthreadd n'appelle jamais `wait`, et init ne récolte que ses propres enfants.

### Impact

Chaque kthread tué conserve pour toujours sa `struct task`, ses 2 pages de pile noyau (8 Kio), son PID et sa place dans la liste parcourue à chaque tick.

### Problème lié [lecture]

Un kthread ne peut pas non plus se terminer de lui-même. Son trap frame initial est posé en haut de sa pile noyau (`src/task.c:263`). Après le premier `iret`, la pile pointe sur un mot nul : si la fonction du kthread retournait, `ret` sauterait à l'adresse 0. Les fonctions de test sont des boucles infinies, donc le cas ne se présente pas aujourd'hui.

### Correction proposée

- **Un faucheur noyau.** La boucle idle du PID 0 (`src/kernel.c:1402`) récolte les kthreads zombies, interruptions coupées (voir point 11). La récolte doit se faire depuis un autre contexte que celui du kthread mourant, qui tourne encore sur sa propre pile au moment du kill.
- **Une sortie propre.** Placer en haut de la pile initiale l'adresse d'une fonction `kthread_exit()` qui appelle `do_exit`. Un kthread dont la fonction retourne se terminerait alors proprement.

---

## Point 4 : Stack and heap of a process

**Exigence** : « Stack and heap of a process. (More information below) ». La section « More information below » n'a pas été fournie ; l'évaluation porte sur l'exigence telle qu'elle est écrite.

**Statut** : partiel. La pile existe, le tas non.

### Ce qui existe

- **La mémoire virtuelle** : un page directory par processus, mapping récursif, `map_page`/`unmap_page`/`alloc_page`/`free_page`, copie complète au fork, libération à la récolte.
- **Des allocateurs avec leur propre « break »** : `kmem_brk` (`src/memory/kmem_dyn_alloc.c:95`) et `vbrk` (`src/memory/vmalloc.c:172`).
- **Les piles** [exécution] :
  - une pile noyau de 2 pages par tâche (`include/task.h:12`) ;
  - une pile utilisateur d'une page par processus ring 3 (`src/cpu/paging.c:376`).

### Pourquoi ce n'est pas le tas du processus

Ces allocateurs gèrent la mémoire **du noyau** : un tas global unique, partagé par tous, au-dessus de 0xC0000000.

Le tas d'un processus est autre chose :
- une région **privée de son espace utilisateur**, sous 0xC0000000 ;
- délimitée par son *break* (`heap_brk`), que le processus fait grandir à la demande (`brk`/`sbrk`, sur lesquels s'appuierait un `malloc` utilisateur) ;
- copiée au fork et libérée à l'exit.

### Espace utilisateur actuel d'un processus

Il contient exactement deux pages (`src/cpu/paging.c:362`) :

```
0xFFFFFFFF ┌──────────────────────────┐
           │ noyau (commun à tous)    │ tas noyau, kmalloc, piles noyau, vmalloc
0xC0000000 ├──────────────────────────┤
           │ pile utilisateur, 1 page │ 0xBFFFF000        ← existe, taille fixe
           │            ↓             │
           │          (rien)          │
           │            ↑             │
           │ tas (break = heap_brk)   │                   ← n'existe pas
           ├──────────────────────────┤
           │ code, 1 page             │ 0x08048000        ← existe
           ├──────────────────────────┤
           │ (non mappé)              │
0x00400000 │ noyau identity, ring 0   │ 0–4 Mio, inaccessible au ring 3
0x00000000 └──────────────────────────┘
```

### Constats [lecture]

- `mm.heap_brk` (`include/task.h:55`) n'est jamais initialisé. Sa seule utilisation est la copie au fork (`src/task.c:534`).
- `data_start` et `data_end` ne sont jamais renseignés.
- L'image d'un processus est limitée à une page (`src/cpu/paging.c:364`).
- La pile ne grandit pas : une faute de page en ring 3 tue le processus par `SIGSEGV` (`src/cpu/isr.c:194`).

### Correction proposée

Elle s'appuie sur le point 9 :
- définir le début du tas, juste après le code et les données, et initialiser `heap_brk` à la création du processus ;
- une fonction qui déplace le break, en mappant ou démappant des pages entre l'ancien et le nouveau break ;
- éventuellement, une pile utilisateur de plusieurs pages.

fork et la récolte parcourent déjà tout l'espace utilisateur (PDE 1 à 767). Les pages du tas seront donc copiées et libérées sans rien modifier.

---

## Point 9 : Functions to work on the memory of a process

**Exigence** : « Functions to work on the memory of a process. »

**Statut** : partiel. Les fonctions de cycle de vie existent. En revanche, rien ne permet de faire évoluer la mémoire d'un processus vivant, ni d'y accéder de façon sûre depuis le noyau.

### Ce qui est attendu

C'est une interprétation, faute de la section « More information below ». On attend en général quatre familles de fonctions :

| Famille | Exemples | HekaOS |
|---|---|---|
| Cycle de vie de l'espace | créer, dupliquer, détruire | ✅ |
| Faire évoluer un processus vivant | mapper/démapper une page dans *son* espace, déplacer son break, éventuellement `mmap`/`munmap` anonymes | ❌ |
| Accès sûr depuis le noyau | `copy_to_user` / `copy_from_user` : vérifier, puis copier | ❌ |
| Informations et droits | taille, régions, code en lecture seule | ❌ |

### Ce qui existe

Ces fonctions sont exercées à l'exécution, indirectement, par spawn, fork et la récolte :
- création de l'espace : `paging_proc_init` (`src/cpu/paging.c:181`) ;
- chargement de l'image : `load_user_page` et `paging_load_user_image` (`src/cpu/paging.c:294`, `src/cpu/paging.c:362`) ;
- duplication : `paging_fork_copy` (`src/cpu/paging.c:219`) ;
- destruction : `paging_proc_teardown` (`src/cpu/paging.c:384`).

### Ce qui manque [lecture]

- **Faire évoluer un processus vivant** : ni map/unmap dans son espace, ni déplacement du break.
- **Accès sûr** : `user_range_ok` (`src/cpu/syscall.c:27`) vérifie la plage d'adresses, pas le mapping. Un `write` depuis le ring 3 sur une adresse dans la bonne plage mais non mappée provoque une faute de page en mode noyau, donc un `kpanic`.
- **Droits** : le code utilisateur est mappé en écriture (`src/cpu/paging.c:332`).

### API proposée

Ce sont des helpers noyau, pas forcément des syscalls dès maintenant :

```c
int   proc_map_page   (struct task *t, uint32_t va, uint32_t flags);
void  proc_unmap_page (struct task *t, uint32_t va);
void *proc_sbrk       (struct task *t, int32_t increment);
int   copy_to_user    (void *dst_user, const void *src, uint32_t n);
int   copy_from_user  (void *dst, const void *src_user, uint32_t n);
```

`load_user_page` (`src/cpu/paging.c:294`) sait déjà mapper une page dans le page directory d'un *autre* processus, via une adresse temporaire. C'est la base à généraliser pour `proc_map_page`.

---

## Point 10 : fork

**Exigence** : « Function to copy an entire process (fork) »

**Statut** : conforme [exécution]. Le PID 3 forke le PID 4 ; l'enfant fait `exit(1)`, le parent fait `wait` puis `exit(0)`, et les deux sont récoltés. Problème : fuite mémoire si la copie échoue.

### Le chemin d'erreur (`src/task.c:490`)

```c
child->mm.pgdir = paging_proc_init (child->pid);
if (paging_fork_copy (child->pid) != 0)
{
    for (uint32_t f = 0; f < KSTACK_PAGES; f++)
    {
        free_page ((void *)(kstack_va + f * PAGE_SIZE));
    }
    kfree (child);
    pid_release ();      /* les pages déjà copiées ne sont pas libérées */
    return -1;
}
```

### Analyse [lecture]

1. `paging_fork_copy` copie l'espace du parent page par page. Pour chaque table de pages, elle alloue une frame pour la table de l'enfant ; pour chaque page présente, une frame qu'elle remplit et installe aussitôt dans le page directory de l'enfant.
2. Si la mémoire physique manque en cours de route (`src/cpu/paging.c:238`, `src/cpu/paging.c:263`), elle renvoie -1 en laissant derrière elle tout ce qu'elle a déjà alloué.
3. `task_fork` n'appelle pas `paging_proc_teardown` sur ce chemin. Les frames restent marquées « utilisées » dans le bitmap du PMM.
4. La seule trace qui les référence, `page_directories[pid]`, est écrasée par le prochain `paging_proc_init` sur ce PID, que `pid_release` vient de rendre. Ces frames sont perdues jusqu'au redémarrage.

`spawn_user` fait correctement ce nettoyage dans le même cas (`src/task.c:347`) ; fork l'a simplement oublié.

### Impact

Il n'y a d'impact qu'en manque de mémoire, mais c'est précisément là que ça fait mal. Avec un fork bomb, une fois les PID recyclés, chaque fork raté ferait fuir de la mémoire et accélérerait l'épuisement.

### Correction proposée

Appeler `paging_proc_teardown (child->pid)` avant de libérer la pile noyau. La fonction ne libère que les entrées présentes, donc elle fonctionne aussi sur une copie partielle.

---

## Point 11 : wait

**Exigence** : « wait » (helper préparant le syscall)

**Statut** : conforme [exécution]. `wait` bloque jusqu'à `SIGCHLD`, récolte l'enfant et renvoie son PID ; c'est observé dans chaque scénario. Problèmes [lecture] : des sections critiques s'exécutent interruptions actives, et le pointeur fourni par l'utilisateur n'est pas validé.

### Contexte

Le noyau n'a pas de verrous. Sa seule protection est que les syscalls et les IRQ s'exécutent interruptions coupées (`cli` à l'entrée des stubs, `src/cpu/isr_stubs.s:4`). Sur un seul CPU, rien ne peut alors s'intercaler.

### La course

`sys_wait` est le seul syscall qui rallume les interruptions pour dormir (`src/cpu/syscall.c:199`), et il ne les recoupe jamais en se réveillant :

```
parent, dans sys_wait                       reste du système
─────────────────────────────────────────   ──────────────────────────────
entrée syscall : IF=0
pas de zombie → BLOCKED
sti ; hlt ────────────────────────────────► tick : bascule vers l'enfant
                                            l'enfant fait exit → ZOMBIE
                                            SIGCHLD → parent RUNNABLE
reprise ◄────────────────────────────────── tick : retour au parent
IF=1 désormais !
scan → zombie trouvé
task_reap (zombie)  ← un tick ou une touche clavier peut couper ici
```

C'est le chemin normal de tout `wait` qui a dû attendre. `task_reap` touche alors, sans protection, à des structures partagées :
- la liste des tâches et l'arbre des processus ;
- les allocateurs (`kfree`, `src/memory/kmalloc.c:142`, et `kmem_free`, `src/memory/kmem_dyn_alloc.c:164`), qui ne sont pas réentrants ;
- surtout, une adresse virtuelle temporaire fixe (`FORK_SCRATCH_PT_VA` = 0xEF200000, `src/cpu/paging.c:214`). Elle est commune à tous les espaces d'adressage, et `paging_proc_teardown` y mappe les tables du zombie (`src/cpu/paging.c:399`).

Si `task_reap` est interrompu à ce moment et que quelqu'un d'autre utilise la même adresse temporaire ou le même allocateur, il reprend sur un état qui a changé sous ses pieds. Il peut alors lire la mauvaise table, libérer les mauvaises frames, ou faire une faute (donc un `kpanic`). Ce « quelqu'un » peut être :
- un fork d'un autre processus (`src/cpu/paging.c:244`) ;
- un `spawntsk -u` tapé au clavier (`src/cpu/paging.c:309`), puisque le shell s'exécute dans l'IRQ clavier (`src/cpu/isr.c:358`) ;
- le `wait` d'un autre parent.

### Le réveil perdu

C'est le deuxième effet de la même cause :

1. Le parent est réveillé sans qu'aucun enfant soit zombie. Tout signal réveille une tâche bloquée (`src/signal.c:110`) ; le hook de test de l'ordonnanceur réveille aussi toutes les tâches bloquées au 4ᵉ tour (`src/sched.c:72`).
2. Il rescanne ses enfants avec IF=1. Un tick peut tomber entre « aucun zombie » et « je repasse BLOCKED ».
3. Si l'enfant meurt dans cet intervalle, son `SIGCHLD` arrive alors que le parent est encore RUNNABLE, et ne réveille donc rien.
4. Le parent se rendort avec un zombie en attente. init s'en remet au décès suivant ; un parent qui n'a qu'un enfant reste bloqué.

Les deux fenêtres sont étroites et rien n'a été observé pendant les tests, mais elles sont réelles.

### Le pointeur non validé

`waitpid` reçoit de l'espace utilisateur l'adresse où écrire le code de sortie, et le noyau y écrit directement (`src/cpu/syscall.c:191`). Il ne vérifie ni que l'adresse appartient à l'espace utilisateur du processus, ni qu'elle est mappée :
- une adresse non mappée provoque une faute de page *en mode noyau*, donc un `kpanic` : un programme bogué fait tomber tout le système ;
- une adresse hors de son espace amène le noyau à écraser de la mémoire qui n'appartient pas au processus.

Règle générale : le noyau ne déréférence jamais un pointeur venu de l'utilisateur sans l'avoir validé.

### Correction proposée

Pour la course et le réveil perdu, recouper les interruptions au réveil, avant de rescanner :

```c
while (current_task->state == TASK_BLOCKED)
{
    enable_interrupts ();
    halt_cpu ();
}
disable_interrupts ();   /* retour en section critique */
```

À terme, il faudra une vraie file d'attente bloquer/réveiller, dont les sockets auront de toute façon besoin.

Pour le pointeur, écrire le status via `copy_to_user` (point 9). Ce helper vérifie la plage, puis que chaque page touchée est présente, accessible au ring 3 et inscriptible, et renvoie une erreur sinon. `read`/`write` et la construction du sigframe devront passer par le même helper.

---

## Point 12 : exit

**Exigence** : « exit » (helper préparant le syscall)

**Statut** : conforme pour les processus ordinaires [exécution], avec un bug critique sur le PID 1.

### Constat [exécution]

`kill 1 9`, alors qu'init a un enfant vivant, bloque le noyau. Le clavier et l'affichage ne répondent plus, sans aucun message. Le CPU tourne en boucle entre `do_exit`, `task_reparent` et `task_add_child`, interruptions coupées.

### Est-ce normal ?

Non. Il y a deux problèmes distincts.

**1. La politique : le PID 1 devrait être protégé.** Dans un Unix, init est spécial parce que c'est lui qui récolte les orphelins :
- Linux ne lui livre que les signaux pour lesquels il a installé un handler, et ignore SIGKILL/SIGSTOP envoyés depuis l'espace utilisateur ;
- s'il meurt quand même, le noyau panique *volontairement*, avec un message explicite (« Attempted to kill init! »).

Tuer init est donc fatal par conception, mais le noyau doit refuser ou s'arrêter proprement, pas se figer en silence.

**2. Le bug : une boucle infinie** dans `do_exit` (`src/task.c:429`) :

```c
struct task *reaper = init_proc ? init_proc : &init_task;  /* = init lui-même */
while (t->children != NULL)
    task_reparent (t->children, reaper);  /* retire l'enfant d'init, puis le remet en tête d'init */
```

`task_reparent` (`src/task.c:413`) retire l'enfant de la liste d'init puis le réinsère en tête de cette même liste : `t->children` ne devient jamais NULL. Comme cela s'exécute pendant le tick, interruptions coupées, la machine se fige.

Si init n'a pas d'enfant au moment où on le tue, il devient un zombie que personne ne récolte. Les processus lancés ensuite sont alors rattachés à ce zombie [lecture].

### Même problème pour le PID 0 [lecture]

Dans un Unix, `kill 0` ne vise pas la tâche idle : il désigne le groupe de processus de l'appelant. Ici, `kill 0 9` transforme la tâche idle en zombie, et son `do_exit` rattache init… à lui-même : init devient son propre parent et son propre enfant.

### Correction proposée

Deux protections complémentaires :
- **Refuser les signaux fatals vers les PID 0 et 1**, dans `kernel_signal_send` (`src/signal.c:80`), ou dans `sys_kill` (`src/cpu/syscall.c:109`) et la commande shell `kill` (`src/kernel.c:628`).
- **Paniquer explicitement** : dans `do_exit`, si la tâche qui meurt est init, appeler `kpanic ("attempted to kill init")` au lieu de boucler.

---

## Conclusion

| Ordre | Point | Action | Effort | Dépend de |
|---|---|---|---|---|
| 1 | 12 | protéger les PID 0 et 1, `kpanic` explicite dans `do_exit` | petit | — |
| 2 | 11 | `disable_interrupts ()` au réveil dans `sys_wait` | petit | — |
| 3 | 10 | `paging_proc_teardown` sur le chemin d'erreur de fork | une ligne | — |
| 4 | 3 | faucheur de kthreads, `kthread_exit ()` | moyen | 11 (récolte interruptions coupées) |
| 5 | 9 | `copy_to_user`/`copy_from_user`, puis `proc_map_page`/`proc_unmap_page` | moyen à gros | — |
| 6 | 11 | status de `wait` écrit via `copy_to_user` | petit | 9 |
| 7 | 4 | tas : initialiser `heap_brk`, `proc_sbrk` ; pile plus grande en option | moyen | 9 |

Les trois premières actions sont courtes et indépendantes, et suppriment les deux risques les plus graves : le blocage du noyau et la corruption mémoire. Les points 4 et 9 sont des manques fonctionnels plutôt que des bugs ; leur périmètre exact dépend de la section « More information below » du cahier des charges, qui reste à fournir.
