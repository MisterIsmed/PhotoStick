#ifndef PHOTOSTICK_H
#define PHOTOSTICK_H

void setBacklight(bool on);
void initSdCard();
void setup();
void animate();
uint16_t getAnimationSteps(Animation anim);
void loop();


#endif // PHOTSTICK_H
