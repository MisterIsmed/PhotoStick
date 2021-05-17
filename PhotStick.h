#ifndef PHOTSTICK_H
#define PHOTSTICK_H

void setBacklight(bool on);
void initSdCard();
void setup();
void animate();
uint16_t getAnimationSteps(Animation anim);
void loop();


#endif // PHOTSTICK_H
