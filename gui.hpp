#ifndef GUI_HPP
#define GUI_HPP

#include "GUIslice.h"
#include "GUIslice_drv.h"
#include "GUIslice_ex.h"
#include "util.hpp"

namespace Gui
{
void init();
void update();
bool readyToGo(StickConfig &cfg);
}

#endif

// vim: et ts=2
