/*
	Bouquet localization

	Copyright (C) 2012 martii

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <global.h>
#include <system/localize_bouquetnames.h>

#include <string>
#include <zapit/bouquets.h>

extern CBouquetManager *g_bouquetManager;

void localizeBouquetNames (void) {
        for (int i = 0, size = (int) g_bouquetManager->Bouquets.size(); i < size; i++)
		if (g_bouquetManager->Bouquets[i]->bFav)
			g_bouquetManager->Bouquets[i]->lName = g_Locale->getText(LOCALE_FAVORITES_BOUQUETNAME);
		else  if (g_bouquetManager->Bouquets[i]->bOther)
			g_bouquetManager->Bouquets[i]->lName = g_Locale->getText(LOCALE_BOUQUETNAME_OTHER);
		else if (g_bouquetManager->Bouquets[i]->Name == "bouquetname.new")
			g_bouquetManager->Bouquets[i]->lName = g_Locale->getText(LOCALE_BOUQUETNAME_NEW);
		else if (g_bouquetManager->Bouquets[i]->Name == "channellist.head")
			g_bouquetManager->Bouquets[i]->lName = g_Locale->getText(LOCALE_CHANNELLIST_HEAD);
		else
			g_bouquetManager->Bouquets[i]->lName = g_bouquetManager->Bouquets[i]->Name;
}
