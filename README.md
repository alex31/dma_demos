# Démonstrations pour l'article de Hackable sur le DMA

### Une machine virtuelle pour vmware vmplayer est disponible :
l'adresse tynyurl donnée dans l'article du Hackable N°36 est fausse, et je m'en excuse.
Le lien direct pour télécharger l'image d'une machine virtuelle vmplayer avec kubuntu 20.04 et les outils pré-installés :

https://drive.google.com/file/d/1tjipGJ9_paJ0SmH0m0Fd9BMzLjV12olb/view?usp=sharing

Vmware vmplayer est une application gratuite dans le cadre d'une utilisation personnelle.
Je vous invite à la télécharger sur le site de l'éditeur.

### Installation préalable des outils de developpement sur une machine ubuntu



>cross compilation et librairies perl pour l'outil de conversion boardGen.pl : board.cfg -> board.h

``` shell
sudo apt install build-essential gcc-arm-none-eabi gdb-multiarch \
         default-jre libmodern-perl-perl \
	 libxml-libxml-perl libfile-which-perl \
	 stlink-tools screen openocd
	 
sudo cpan -i String/LCSS.pm
```
> to use bmpflash : necessite de se delogger et relogger
``` shell
sudo usermod -a -G dialout $USER
```
### installation stm32cubemx depuis le site de stmicroelectronics :
https://www.st.com/en/development-tools/stm32cubemx.html
(le script boardGen.pl a besoin des fichier xml de description des MCU)


### clone du depot 
> contenant les exemples, et des sous modules : chibios, et un ensemble de librairies utilitaires developpées à l'Ecole Nationale de l'Aviation Civile
``` shell
git clone --recursive https://github.com/alex31/dma_demos.git
```
### une fois l'image clonée
> mise à jour locale des demos (impératif en cas d'utilisation de la machine virtuelle) se fera par :
``` shell
cd ~/dma_demos; git pull ; git submodule update
```


## Démos correspondant à l'article de HACKABLE N° 36

### La première démo : un chenillard avec les 4 LEDs soudées sur la carte discoveryF4. 
- Une tâche fait périodiquement flasher les 4 leds d'un coup à 1Hz.
- Après un appui sur le bouton USER bleu, Chibios est stoppé
- le flash périodique s'arrète, mais le DMA continue d'animer le chenillard sans intervention du CPU qui est arreté.
``` shell
cd dma_demos/disco_led_blink; make -j4
```
>brancher la carte, monter le disque USB DIS_F407VG, puis flasher le binaire :
``` shell
cp build/ch.bin /media/xxx/DIS_F407VG
```
### la deuxième démo fait de l'acquition par l'ADC
> de 2 sources internes : la reference interne de 1.21V, et la termistance qui mesure la temperature du coeur.
- Le pgm commence par faire de l'acquisition one shot
- après un appui sur le bouton USER bleu : fait de l'acquisition continue :
``` shell
cd ../disco_adc; make -j4
```
- brancher la carte, monter le disque USB DIS_F407VG
- flasher le binaire : si dans une machine virtuelle : transferer la sonde de prog vers la machine virtuelle : menu VM -> Removable Device ->STMicroelectronics STM32 STLink -> Connect 
``` shell
cp build/ch.bin /media/xxx/DIS_F407VG
```
- lancer un terminal pour lire la sortie de debug sur la liaison série qui affiche tension et température :
``` shell
screen /dev/ttyACM0 115200
```
### la troisieme démo lit une UART
``` shell
cd ../disco_continuous_uart; make -j4
```
- brancher la carte
- monter le disque USB DIS_F407VG, puis flasher le binaire : si dans une machine virtuelle : transferer la sonde de prog vers la machine virtuelle : menu VM -> Removable Device ->STMicroelectronics STM32 STLink -> Connect 
``` shell
cp build/ch.bin /media/xxx/DIS_F407VG
```
- brancher deux convertisseur USB-Serie, pour le premier, brancher son TX sur PB07 (connecteur P2:24)
- pour le deuxième brancher son RX sur PB10 (connecteur P1:34) connecter les masses des câbles USB-Série sur des broches GND (il y en a plusieurs)
- il est maintenant possible depuis linux d'écrire à 230400 bauds sur la première liaison série, et recuperer le flux sur l'autre liaison série à 115200 bauds, sans utiliser le cpu, sans perte tant qu'on ne sature pas le buffer de 1Ko



### la quatrieme demo pilote des LEDs WS2812
``` shell
cd ../disco_ws2812_strip; make -j4
```
- brancher la carte, monter le disque USB DIS_F407VG, puis flasher le binaire 
- si dans une machine virtuelle : transferer la sonde de prog vers la machine virtuelle :
- menu VM -> Removable Device ->STMicroelectronics STM32 STLink -> Connect 
``` shell
cp build/ch.bin /media/xxx/DIS_F407VG
```
-brancher de 1 à 4 bandes de LEDs WS2812B sur les broches PA08 (connecteur P2:43)
- PA10 (connecteur P2:41), PE11 (connecteur P1:29), PE14 (connecteur P1:32)
- brancher aussi les alimentation des ledstrip, si c'est des ledstrip jusqu'à 8 leds, le 5V de la carte sera suffisant, si c'est des ledstrip plus grands, il faudra qu'ils aient leur propre alimentation.
> lancer un terminal pour lire la sortie de debug sur la liaison série qui affiche le temps passé dans l'ISR
``` shell
screen /dev/ttyACM0 115200
```
> vous pouvez ajuster la taille du LEDSTRIP dans le fichier source, en editant la valeur de la macro  STRIP_NB_LEDS


## Démos correspondant à l'article de HACKABLE N° 38

### La première démo : copie mémoire à mémoire
- Un benchmark des performances de copie mémoire, en fonction de la taille du segment copié
  
- Après un appui sur le bouton USER bleu, un benchmark est lancé sur un système sans charge et les performances sont affichées sur le terminal connecté par la sonde de programmation à la liaison série du MCU.
- Après un second appui sur le bouton USER bleu, un benchmark est lancé sur un système très chargé à la fois au niveau CPU et au niveau transactions sur les bus mémoire. Les performances sont affichées sur le terminal connecté par la sonde de programmation à la liaison série du MCU.

``` shell
cd dma_demos/disco_memory_copy; make -j4
```
>brancher la carte, monter le disque USB DIS_F407VG, puis flasher le binaire :
``` shell
cp build/ch.bin /media/xxx/DIS_F407VG
```
> lancer un terminal pour lire la sortie de debug sur les résultats des benchmarks
``` shell
screen /dev/ttyACM0 115200
```

### démo N° 2 : multiplexeur
- Un multiplexeur de port à 2 entrées, une sortie sur 4 bits

- Les deux entrées du multiplexeur sont :
    * B4-7 (configurés en PULLDOWN, donc à 0 si non branché)
    * B12-15 (configurés en PULLUP, donc à 1 si non branché)

- la sortie est :
    * D12-15 (connectés à 4 leds sur la carte discovery)

- deux autres broches sont configurées en sortie :
    * A8 (clignotement lent, à connecter avec un fil dupont à B4)
    * A9 (clignotement rapide, à connecter avec un fil dupont à B12)

B4-7 est recopié à 42Mhz sur D12-15. Si l'on appuie sur le bouton bleu, c'est B12-15 qui est recopié. Si l'on a correctement relié A8 à B4 et A9 à B12, on constate sur l'affichage des LEDs que le recopie fonctionne. Le taux de recopie est affiché dans la console en MO/s

``` shell
cd dma_demos/disco_memory_copy; make -j4
```
> relier avec deux fils dupont les ports A8 et B4;  A9 et B12
>brancher la carte, monter le disque USB DIS_F407VG, puis flasher le binaire :
``` shell
cp build/ch.bin /media/xxx/DIS_F407VG
```
> lancer un terminal pour lire la sortie de debug sur les résultats des benchmarks
``` shell
screen /dev/ttyACM0 115200
```

> jouer avec le bouton bleu et constater le fonctionnement du multiplexeur.


### la troisieme démo lit une UART en mode zero copy

Cette demo est fonctionnelement identique à la démo disco_continuous_uart, mais illustre le fonctionnement en double buffer pour ne pas solliciter le cpu du tout.

``` shell
cd ../disco_continuous_uart_zerocopy; make -j4
```
- brancher la carte
- monter le disque USB DIS_F407VG, puis flasher le binaire : si dans une machine virtuelle : transferer la sonde de prog vers la machine virtuelle : menu VM -> Removable Device ->STMicroelectronics STM32 STLink -> Connect 
``` shell
cp build/ch.bin /media/xxx/DIS_F407VG
```
- brancher deux convertisseur USB-Serie, pour le premier, brancher son TX sur PB07 (connecteur P2:24)
- pour le deuxième brancher son RX sur PB10 (connecteur P1:34) connecter les masses des câbles USB-Série sur des broches GND (il y en a plusieurs)
- il est maintenant possible depuis linux d'écrire à 230400 bauds sur la première liaison série, et recuperer le flux sur l'autre liaison série à 115200 bauds, sans utiliser le cpu, sans perte tant qu'on ne sature pas le buffer de 1Ko

### la quatrieme démo surveille la tension de l'USB en mode zero copy

Cette demo utilise le fonctionnement double buffer zerocopy pour faire une acquisition ADC continue qui surveille la tension du bus USB

``` shell
cd ../disco_adc_zerocopy_usb_voltage; make -j4
```
- brancher la carte
- monter le disque USB DIS_F407VG, puis flasher le binaire : si dans une machine virtuelle : transferer la sonde de prog vers la machine virtuelle : menu VM -> Removable Device ->STMicroelectronics STM32 STLink -> Connect 
``` shell
cp build/ch.bin /media/xxx/DIS_F407VG
```
- brancher un pont de resistances de 10Kohms entre une broche 5V et la masse
- brancher le point milieu de ce pont de résistances sur PC01
- lire dans la console les alertes sur des possibles variation de tension quand on branche
  un périphérique sur un port USB.


### La dernière démo : gestion des contraintes dues au cache mémoire du STM32F7
- Un benchmark des performances de copie mémoire, en fonction de la taille du segment copié qui fonctionne sur discovery F767. Sur cette carte, un UART du MCU est déjà reliée à la sonde de programmation, pas de modification à prévoir comme on a du le faire sur la discovery F407
  
- un benchmark est lancé sur un système sans charge et les performances sont affichées sur le terminal connecté par la sonde de programmation à la liaison série du MCU.
- par defaut de la mémoire utilisant un cache est utilisée pour la destination et on peut voir que la vérification échoue
- il faut commenter la ligne 107 et decommenter la ligne 108 de main.c, recompiler, reflasher pour que leca fonctionne.

``` shell
cd dma_demos/nucleof7_memory_copy; make -j4
```
>brancher la carte, monter le disque USB NOD_F767ZI, puis flasher le binaire :
``` shell
cp build/ch.bin /media/xxx/NOD_F767ZI
```
> lancer un terminal pour lire la sortie de debug sur les résultats des benchmarks
``` shell
screen /dev/ttyACM0 115200
```
