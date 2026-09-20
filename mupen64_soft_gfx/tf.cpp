/**
 * Mupen64 - tf.cpp
 * Copyright (C) 2002 Hacktarux
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 * 
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
**/

#include <stdio.h>

#include "tf.h"

TF::TF()
{
   textureFilter=0; textureConvert=6;
}

TF::~TF()
{
}

void TF::setTextureFilter(int value)
{
   textureFilter = value;
}

void TF::setTextureConvert(int value)
{
   textureConvert = value;
}

int TF::getTextureConvert()
{
   return textureConvert;
}

Color32 TF::filter(Color32 nearestTexels[4], float nearestTexelsDistance[4])
{
   if (textureFilter == 0)
     {
	float min = nearestTexelsDistance[0];
	int minIndex = 0;
	for (int i=0; i<4; i++)
	  {
	     if (nearestTexelsDistance[i] < min)
	       {
		  min = nearestTexelsDistance[i];
		  minIndex = i;
	       }
	  }
	return nearestTexels[minIndex];
     }
   else if (textureFilter == 2 || textureFilter == 3)
     {
       float sf=(nearestTexelsDistance[0]-nearestTexelsDistance[1]+1)*0.5f;
       float tf=(nearestTexelsDistance[0]-nearestTexelsDistance[3]+1)*0.5f;
       float weights[4]={};
       if(textureFilter==3) for(int i=0;i<4;i++) weights[i]=0.25f;
       else if(sf+tf<=1) { weights[0]=1-sf-tf; weights[1]=sf; weights[3]=tf; }
       else { weights[2]=sf+tf-1; weights[1]=1-tf; weights[3]=1-sf; }
       Color32 out(0,0,0,0); float alpha=0;
       for(int i=0;i<4;i++) { out+=nearestTexels[i]*weights[i]; alpha+=nearestTexels[i].getAlpha()*weights[i]; }
       out.setAlpha(alpha); return out;
     }
   return nearestTexels[0];
}
