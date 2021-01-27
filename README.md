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

### La première démo : un chenillard avec les 4 LEDs soudées sur la carte discoveryF4. 
- Une tâche fait périodiquement flasher les 4 leds d'un coup à 1Hz.
- Après un appui sur le bouton USER bleu, Chibios est stoppé
- le flash périodique s'arrète, mais le DMA continue d'animer le chenillard sans intervention du CPU qui est arreté.
``` shell
cd dma_demos/led_blink; make -j4
```
>brancher la carte, monter le disque USB DIS_F407VG, puis flasher le binaire :
``` shell
cp build/ch.bin /mnt/media/xxx/DIS_F407VG
```
### la deuxième démo fait de l'acquition par l'ADC
> de 2 sources internes : la reference interne de 1.21V, et la termistance qui mesure la temperature du coeur.
- Le pgm commence par faire de l'acquisition one shot
- après un appui sur le bouton USER bleu : fait de l'acquisition continue :
``` shell
cd ../adc; make -j4
```
- brancher la carte, monter le disque USB DIS_F407VG
- flasher le binaire : si dans une machine virtuelle : transferer la sonde de prog vers la machine virtuelle : menu VM -> Removable Device ->STMicroelectronics STM32 STLink -> Connect 
``` shell
cp build/ch.bin /mnt/media/xxx/DIS_F407VG
```
- lancer un terminal pour lire la sortie de debug sur la liaison série qui affiche tension et température :
``` shell
screen /dev/ttyACM0 115200
```
### la troisieme démo lit une UART
``` shell
cd ../continuous_uart; make -j4
```
- brancher la carte
- monter le disque USB DIS_F407VG, puis flasher le binaire : si dans une machine virtuelle : transferer la sonde de prog vers la machine virtuelle : menu VM -> Removable Device ->STMicroelectronics STM32 STLink -> Connect 
``` shell
cp build/ch.bin /mnt/media/xxx/DIS_F407VG
```
- brancher deux convertisseur USB-Serie, pour le premier, brancher son TX sur PB07 (connecteur P2:24)
- pour le deuxième brancher son RX sur PB10 (connecteur P1:34) connecter les masses des câbles USB-Série sur des broches GND (il y en a plusieurs)
- il est maintenant possible depuis linux d'écrire à 230400 bauds sur la première liaison série, et recuperer le flux sur l'autre liaison série à 115200 bauds, sans utiliser le cpu, sans perte tant qu'on ne sature pas le buffer de 1Ko



### la quatrieme demo pilote des LEDs WS2812
``` shell
cd ../ws2812_strip; make -j4
```
- brancher la carte, monter le disque USB DIS_F407VG, puis flasher le binaire 
- si dans une machine virtuelle : transferer la sonde de prog vers la machine virtuelle :
- menu VM -> Removable Device ->STMicroelectronics STM32 STLink -> Connect 
``` shell
cp build/ch.bin /mnt/media/xxx/DIS_F407VG
```
-brancher de 1 à 4 bandes de LEDs WS2812B sur les broches PA08 (connecteur P2:43)
- PA10 (connecteur P2:41), PE11 (connecteur P1:29), PE14 (connecteur P1:32)
- brancher aussi les alimentation des ledstrip, si c'est des ledstrip jusqu'à 8 leds, le 5V de la carte sera suffisant, si c'est des ledstrip plus grands, il faudra qu'ils aient leur propre alimentation.
> lancer un terminal pour lire la sortie de debug sur la liaison série qui affiche le temps passé dans l'ISR
``` shell
screen /dev/ttyACM0 115200
```
> vous pouvez ajuster la taille du LEDSTRIP dans le fichier source, en editant la valeur de la macro  STRIP_NB_LEDS
