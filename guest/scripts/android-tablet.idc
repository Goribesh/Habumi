# Dice ad Android di trattare il tablet virtio come PUNTATORE, non come schermo
# tattile.
#
# Perche' serve. Il dispositivo dichiara BTN_TOUCH fra le proprie capacita', e in
# base a quello Android lo classifica come touchscreen. Ma QEMU, misurato con
# getevent, non lo invia mai:
#   864  0003 0000  EV_ABS ABS_X       <- movimento, arriva
#    84  0001 0110  EV_KEY BTN_LEFT    <- clic, arriva
#     0  0001 014a  EV_KEY BTN_TOUCH   <- mai
# Il mapper touchscreen attende BTN_TOUCH per stabilire che il dito e' giu', quindi
# scarta tutto: nessun cursore, nessun tocco, e la finestra sembra ignorare mouse e
# dito mentre la tastiera funziona.
#
# Con deviceType = pointer, Android usa la semantica del mouse: BTN_LEFT diventa un
# clic e appare un cursore. Il tocco vero arriva invece dal dispositivo
# virtio-multitouch, che emette ABS_MT_* e BTN_TOUCH reali.
#
# Il nome del file e' quello del dispositivo con i caratteri non alfanumerici
# sostituiti da underscore, cercato in /vendor/usr/idc/.

touch.deviceType = pointer
touch.orientationAware = 0
