#include "zmap.h"

#include "Object.h"
#include "PlayerObject.h"
#include "MessageLib/MessageLib.h"


zmap* zmap::ZMAP = NULL;

struct SubCell
{
public:
	uint32 subCellId;

	float x;
	float z; 
	float height; 
	float width;

	ZmapSubCellCallback* callback;
};

zmap::zmap()
{
	mCurrentSubCellID = 0;

	ZMAP = this;

	uint32 x = 0, i = 0, j = 0;

	
	// Setup the lookup array...
	i = 0;
	for(x = 0; x <= GRIDWIDTH; x++)
	{
		for(j = 0; j <= GRIDHEIGHT; j++)
		{
			zmap_lookup[x][j] = i;
			i++;

		}
	}
}

zmap::~zmap()
{

}

bool zmap::_isInSubCellExtent(SubCell* subCell, float x, float z)
{
	if(((x >= subCell->x) && (x <= (subCell->x + subCell->height))) && 
		((z >= subCell->z) && (z <= (subCell->z + subCell->width))))
		return true;
	else
		return false;
}

uint32	zmap::AddSubCell(float low_x, float low_z, float height, float width, ZmapSubCellCallback* callback)
{
	SubCell* pSubCell		= new SubCell();
	pSubCell->x				= low_x;
	pSubCell->z				= low_z;
	pSubCell->height		= height;
	pSubCell->width			= width;
	pSubCell->callback		= callback;
	pSubCell->subCellId		= mCurrentSubCellID++;

	uint32 lowerLeft		= _getCellId(low_x,			low_z);
	uint32 lowerRight		= _getCellId(low_x+width,	low_z);
	uint32 upperLeft		= _getCellId(low_x,			low_z+height);
	uint32 upperRight		= _getCellId(low_x+width,	low_z+height);

	unsigned int cellCountZ = (lowerLeft - upperLeft)/410;
	unsigned int cellCountX = (lowerLeft - lowerRight);

	for(unsigned int i=0; i < cellCountZ; i++)
	{
		for(unsigned int j=0; j < cellCountX; j++)
		{
			subCells.insert(std::make_pair((lowerLeft + j + i * 410), pSubCell));
		}
	}

	return pSubCell->subCellId;
}

bool zmap::isObjectInSubCell(Object* object, uint32 subCellId)
{
	std::multimap<uint32, SubCell*>::iterator it;

	std::pair<std::multimap<uint32, SubCell*>::iterator, std::multimap<uint32, SubCell*>::iterator> multi_pairing;

	multi_pairing = subCells.equal_range(object->zmapCellID);

	for(std::multimap<uint32, SubCell*>::iterator it = multi_pairing.first; it != multi_pairing.second; ++it)
	{
		if((*it).second->subCellId == subCellId)
			return _isInSubCellExtent((*it).second, object->mPosition.x, object->mPosition.z);
	}

	return false;
}

void	zmap::RemoveSubCell(uint32 subCellId)
{
	std::multimap<uint32, SubCell*>::iterator it = subCells.begin();
	std::multimap<uint32, SubCell*>::iterator end = subCells.end();

	while(it != end)
	{
		if((*it).second->subCellId == subCellId)
		{
			it = subCells.erase(it);
		}
		else
		{
			++it;
		}
	}
}

uint32 zmap::_getCellId(float x, float z)
{
	return zmap_lookup[((((uint32)z) + 8500)/GRIDWIDTH)][((((uint32)x) + 8500)/GRIDHEIGHT)];
}

bool zmap::AddObject(Object *newObject)
{
	//Pesudo
	// 1. Calculate CellID
	// 2. Set CellID
	// 3. Insert object into the cell in the hash table

	uint32 FinalCell = _getCellId(newObject->mPosition.x, newObject->mPosition.y);

	newObject->zmapCellID = FinalCell;
	std::list<Object*> list;

	if(newObject->getType() == ObjType_Player)
	{
		list = ZMapCells[FinalCell].Players;
	}
	else
	{
		list = ZMapCells[FinalCell].Objects;
	}

	for(std::list<Object*>::iterator i = list.begin(); i != list.end(); i++)
	{
		if((*i)->getId() == newObject->getId())
		{
			return false;
		}
	}

	list.push_back(newObject);
	return true;

}

void zmap::RemoveObject(Object *removeObject)
{
	uint32 cellId = removeObject->zmapCellID;

	std::list<Object*> list;
	if(removeObject->getType() == ObjType_Player)
	{
		list = ZMapCells[cellId].Players;
	}
	else
	{
		list = ZMapCells[cellId].Objects;
	}

	for(std::list<Object*>::iterator i = list.begin(); i != list.end(); i++)
	{
		if((*i)->getId() == removeObject->getId())
		{
			list.erase(i);
			break;
		}
	}

	return;
}

void zmap::UpdateObject(Object *updateObject)
{
	// Pesudo
	// 1. Calculate Cell
	// 2. If cell is different then current cell
	//		2a. Remove him from the current cell, insert into new cell at new cell
	//		2b. Update info....

	uint32 FinalCell = _getCellId(updateObject->mPosition.x, updateObject->mPosition.y);
	
	if(updateObject->zmapCellID != FinalCell)
	{
	
		UpdateBackCells(updateObject,FinalCell);
		UpdateFrontCells(updateObject,FinalCell);
		RemoveObject(updateObject);
		AddObject(updateObject);
	
	}

	//We need to check subregions
	std::multimap<uint32, SubCell*>::iterator it;
	std::pair<std::multimap<uint32, SubCell*>::iterator, std::multimap<uint32, SubCell*>::iterator> multi_pairing;

	multi_pairing = subCells.equal_range(updateObject->zmapCellID);

	for(std::multimap<uint32, SubCell*>::iterator it = multi_pairing.first; it != multi_pairing.second; ++it)
	{
		bool isInRegion = false;
		bool isTrulyInRegion = isObjectInSubCell(updateObject, (*it).second->subCellId);

		std::set<uint32>::iterator subCell = updateObject->zmapSubCells.find((*it).second->subCellId);

		if(subCell != updateObject->zmapSubCells.end())
			isInRegion = true;

		if(isTrulyInRegion && !isInRegion)
		{
			//Has just entered the region
			updateObject->zmapSubCells.insert((*it).second->subCellId);
			(*it).second->callback->ZmapCallback_OnEnterSubCell();
		}
		else if(!isTrulyInRegion && isInRegion)
		{
			//Has just left the region
			updateObject->zmapSubCells.erase(subCell);
			(*it).second->callback->ZmapCallback_OnExitSubCell();
		}
		else
		{
			//No change has occurred.
		}
	}
}

ObjectStruct*	zmap::GetCellContents(uint32 CellID)
{
	//Pesudo
	// 1. Return list of objects in cell
	//if(CellID) >
	
	if(CellID > (GRIDWIDTH*GRIDHEIGHT))
		return &EmptyStruct;

	return &ZMapCells[CellID];
}

std::list<Object*>*	zmap::GetAllCellContents(uint32 CellID)
{
	//Pesudo
	// 1. Return list of objects in cell
	//if(CellID) >
	
	if(CellID > (GRIDWIDTH*GRIDHEIGHT))
		return &EmptyCell;

	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = ZMapCells[CellID].Objects;
	ReturnList->splice(it, temp);

	temp = ZMapCells[CellID].Players;
	ReturnList->splice(it, temp);

	return ReturnList;
}

std::list<Object*>*	zmap::GetPlayerCellContents(uint32 CellID)
{
	//Pesudo
	// 1. Return list of objects in cell
	//if(CellID) >
	
	if(CellID > (GRIDWIDTH*GRIDHEIGHT))
		return &EmptyCell;

	return &ZMapCells[CellID].Players;
}

std::list<Object*>*	zmap::GetObjectCellContents(uint32 CellID)
{
	//Pesudo
	// 1. Return list of objects in cell
	//if(CellID) >
	
	if(CellID > (GRIDWIDTH*GRIDHEIGHT))
		return &EmptyCell;
	
	return &ZMapCells[CellID].Objects;
}

//=================================================
//returns Players in chatrange

std::list<Object*>* zmap::GetChatRangeCellContents(uint32 CellID)
{
	//Pesudo
	// 1. Combine the lists of Neiboring cells to 1

	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetPlayerCellContents(CellID);
	ReturnList->splice(it, temp);

	temp = *GetPlayerCellContents(CellID + GRIDWIDTH);
	ReturnList->splice(it, temp);

	temp = *GetPlayerCellContents(CellID - GRIDWIDTH);
	ReturnList->splice(it, temp);

	temp = *GetPlayerCellContents(CellID + 1);
	ReturnList->splice(it, temp);

	temp = *GetPlayerCellContents(CellID - 1);
	ReturnList->splice(it, temp);

	temp = *GetPlayerCellContents(CellID + GRIDWIDTH-1);
	ReturnList->splice(it, temp);

	temp = *GetPlayerCellContents(CellID + GRIDWIDTH+1);
	ReturnList->splice(it, temp);

	temp = *GetPlayerCellContents(CellID - GRIDWIDTH+1);
	ReturnList->splice(it, temp);

	temp = *GetPlayerCellContents(CellID - GRIDWIDTH-1);
	ReturnList->splice(it, temp);


	return ReturnList;
}

//=====================================================
//Get Contents for a Row based on a middle cell
//depending on how far we are away of the player we need to resize the row
//this will be done by the var iteration

std::list<Object*>* zmap::GetAllGridContentsListRow(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	ObjectStruct temp = *GetCellContents(CellID);
	ReturnList->splice(it, temp.Objects);
	ReturnList->splice(it, temp.Players);
	

	for(int i = 1; i <= VIEWRANGE; i++)
	{
		//if(fmod(CellID+1,))
		temp = *GetCellContents(CellID + i);
		ReturnList->splice(it, temp.Objects);
		ReturnList->splice(it, temp.Players);
		
		temp = *GetCellContents(CellID - i);
		ReturnList->splice(it, temp.Objects);
		ReturnList->splice(it, temp.Players);
	}

	return ReturnList;

}

std::list<Object*>* zmap::GetPlayerGridContentsListRow(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	ObjectStruct temp = *GetCellContents(CellID);
	ReturnList->splice(it, temp.Players);
	

	for(int i = 1; i <= VIEWRANGE; i++)
	{
		//if(fmod(CellID+1,))
		temp = *GetCellContents(CellID + i);
		ReturnList->splice(it, temp.Players);
		
		temp = *GetCellContents(CellID - i);
		ReturnList->splice(it, temp.Players);
	}

	return ReturnList;

}

std::list<Object*>* zmap::GetObjectGridContentsListRow(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	ObjectStruct temp = *GetCellContents(CellID);
	ReturnList->splice(it, temp.Objects);
	

	for(int i = 1; i <= VIEWRANGE; i++)
	{
		//if(fmod(CellID+1,))
		temp = *GetCellContents(CellID + i);
		ReturnList->splice(it, temp.Objects);
		
		temp = *GetCellContents(CellID - i);
		ReturnList->splice(it, temp.Objects);
	}

	return ReturnList;

}

//==========================================================================
//column downwards
//when we move along the edges
///15.6 sch
//
std::list<Object*>* zmap::GetPlayerGridContentsListColumnDown(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetPlayerCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= (VIEWRANGE*2)-1; i++)
	{		
		temp = *GetPlayerCellContents(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

std::list<Object*>* zmap::GetObjectGridContentsListColumnDown(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetObjectCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= (VIEWRANGE*2)-1; i++)
	{		
		temp = *GetObjectCellContents(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

std::list<Object*>* zmap::GetAllGridContentsListColumnDown(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();
								 
	std::list<Object*> temp = *GetAllCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= (VIEWRANGE*2)-1; i++)
	{		
		temp = *GetAllCellContents(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

std::list<Object*>* zmap::GetAllGridContentsListColumnUp(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetAllCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= (VIEWRANGE*2)-1; i++)
	{		
		temp = *GetAllCellContents(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

std::list<Object*>* zmap::GetPlayerGridContentsListColumnUp(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetPlayerCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= (VIEWRANGE*2)-1; i++)
	{		
		temp = *GetPlayerCellContents(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

std::list<Object*>* zmap::GetObjectGridContentsListColumnUp(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetObjectCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= (VIEWRANGE*2)-1; i++)
	{		
		temp = *GetObjectCellContents(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

//===============================================================0
// when getting content on the edges just spare the *middle* (pointy) cell
std::list<Object*>* zmap::GetAllGridContentsListRowLeft(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetAllCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i < ((VIEWRANGE*2)-1); i++)
	{
		temp = *GetAllCellContents(CellID - i);
		ReturnList->splice(it, temp);
	}

	return ReturnList;

}

std::list<Object*>* zmap::GetPlayerGridContentsListRowLeft(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetPlayerCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i < ((VIEWRANGE*2)-1); i++)
	{
		temp = *GetPlayerCellContents(CellID - i);
		ReturnList->splice(it, temp);
	}

	return ReturnList;

}

std::list<Object*>* zmap::GetObjectGridContentsListRowLeft(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetObjectCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i < ((VIEWRANGE*2)-1); i++)
	{
		temp = *GetObjectCellContents(CellID - i);
		ReturnList->splice(it, temp);
	}

	return ReturnList;

}

std::list<Object*>* zmap::GetAllGridContentsListRowRight(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetAllCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i < ((VIEWRANGE*2)-1); i++)
	{
		temp = *GetAllCellContents(CellID + i);
		ReturnList->splice(it, temp);
	}

	return ReturnList;

}

std::list<Object*>* zmap::GetPlayerGridContentsListRowRight(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetPlayerCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i < ((VIEWRANGE*2)-1); i++)
	{
		temp = *GetPlayerCellContents(CellID + i);
		ReturnList->splice(it, temp);
	}

	return ReturnList;

}

std::list<Object*>* zmap::GetObjectGridContentsListRowRight(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetObjectCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i < ((VIEWRANGE*2)-1); i++)
	{
		temp = *GetObjectCellContents(CellID + i);
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

std::list<Object*>* zmap::GetAllGridContentsListColumn(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetAllCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= VIEWRANGE; i++)
	{
		//if(fmod(CellID+1,))
		temp = *GetAllCellContents(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
		
		temp = *GetAllCellContents(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;

}

std::list<Object*>* zmap::GetPlayerGridContentsListColumn(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetPlayerCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= VIEWRANGE; i++)
	{
		//if(fmod(CellID+1,))
		temp = *GetPlayerCellContents(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
		
		temp = *GetPlayerCellContents(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;

}


std::list<Object*>* zmap::GetObjectGridContentsListColumn(uint32 CellID)
{
	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetObjectCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 1; i <= VIEWRANGE; i++)
	{
		//if(fmod(CellID+1,))
		temp = *GetObjectCellContents(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
		
		temp = *GetObjectCellContents(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;

}


std::list<Object*>* zmap::GetAllViewingRangeCellContents(uint32 CellID)
{
	//Pesudo
	// 1. Combine the lists of Neiboring cells to 1

	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetAllCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 0; i < VIEWRANGE; i++)
	{
		temp = *GetAllGridContentsListRow(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
		
		temp = *GetAllGridContentsListRow(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

std::list<Object*>* zmap::GetPlayerViewingRangeCellContents(uint32 CellID)
{
	//Pesudo
	// 1. Combine the lists of Neiboring cells to 1

	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetPlayerCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 0; i < VIEWRANGE; i++)
	{
		temp = *GetPlayerGridContentsListRow(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
		
		temp = *GetPlayerGridContentsListRow(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

std::list<Object*>* zmap::GetObjectViewingRangeCellContents(uint32 CellID)
{
	//Pesudo
	// 1. Combine the lists of Neiboring cells to 1

	std::list<Object*>* ReturnList = new std::list<Object*>;
	std::list<Object*>::iterator it = ReturnList->begin();

	std::list<Object*> temp = *GetObjectCellContents(CellID);
	ReturnList->splice(it, temp);

	for(int i = 0; i < VIEWRANGE; i++)
	{
		temp = *GetObjectGridContentsListRow(CellID + (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
		
		temp = *GetObjectGridContentsListRow(CellID - (i*GRIDWIDTH));
		ReturnList->splice(it, temp);
	}

	return ReturnList;
}

//============================================================================
//destroy an object if its not us
void zmap::CheckObjectIterationForDestruction(Object* toBeTested, Object* toBeUpdated)
{
	PlayerObject* us = dynamic_cast<PlayerObject*>(toBeUpdated);

	if(toBeTested->getId() != toBeUpdated->getId())
	{
		//we (updateObject) got out of range of the following (*i) objects
		//destroy them for us
		//if its a player, destroy us for him
		if(us)
		{
			gMessageLib->sendDestroyObject(toBeTested->getId(),us);
		}
				
		PlayerObject* them = dynamic_cast<PlayerObject*> (toBeTested);
		if(them)
		{
			gMessageLib->sendDestroyObject(toBeUpdated->getId(),them);
			
			//now what to do with 
			if(toBeUpdated->getType() == ObjType_Structure)
			{
				//specialized factory despawn
				
			}
		}
	}
}

//=================================================================================
//destroy objects out of our range
//
//
void zmap::UpdateBackCells(Object* updateObject, uint32 newCell)
{
	//are all the cells on our left or right valid ??
	//arnt we looking to the other side of the map ???		we cant the map has a 400m buffer to all sides
	

	//ZMAP Northbound! TODO: Sync with game
	if((updateObject->zmapCellID + GRIDWIDTH) == newCell)
	{
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		FinalList = *GetAllGridContentsListRow(updateObject->zmapCellID - (GRIDWIDTH*VIEWRANGE));

		for(std::list<Object*>::iterator i = FinalList.begin(); i != FinalList.end(); i++)
		{
			CheckObjectIterationForDestruction((*i),updateObject);			
		}	

		return;
	}

	//ZMAP Southbound! TODO: Sync with game
	else if((updateObject->zmapCellID - GRIDWIDTH) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();
		
		std::list<Object*> temp = *GetAllGridContentsListRow(updateObject->zmapCellID + (GRIDWIDTH*VIEWRANGE));
		FinalList.splice(it, temp);

		for(std::list<Object*>::iterator i = FinalList.begin(); i != FinalList.end(); i++)
		{
			CheckObjectIterationForDestruction((*i),updateObject);			
		}

		return;
	}

		//ZMAP Westbound! TODO: Sync with game
	else if((updateObject->zmapCellID - 1) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllGridContentsListRow(updateObject->zmapCellID + VIEWRANGE);
		FinalList.splice(it, temp);

		for(std::list<Object*>::iterator i = FinalList.begin(); i != FinalList.end(); i++)
		{
			CheckObjectIterationForDestruction((*i),updateObject);
		}

		return;
	}

	//Eastbound! TODO: Sync with game
	else if((updateObject->zmapCellID + 1) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllGridContentsListColumn(updateObject->zmapCellID - VIEWRANGE);
		FinalList.splice(it, temp);

		for(std::list<Object*>::iterator i = FinalList.begin(); i != FinalList.end(); i++)
		{
			CheckObjectIterationForDestruction((*i),updateObject);
		}

		return;
	}

	// NorthEastbound
	else if((updateObject->zmapCellID + (GRIDWIDTH+1)) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllCellContents(updateObject->zmapCellID - (GRIDWIDTH+1)*VIEWRANGE);
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListColumnUp(updateObject->zmapCellID - ((GRIDWIDTH+1)*VIEWRANGE) +GRIDWIDTH );//		FinalList.splice(it, temp);
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListRowRight(updateObject->zmapCellID - ((GRIDWIDTH+1)*VIEWRANGE) + 1);//
		FinalList.splice(it, temp);

		for(std::list<Object*>::iterator i = FinalList.begin(); i != FinalList.end(); i++)
		{
			CheckObjectIterationForDestruction((*i),updateObject);
		}

		return;
	}

	// NorthWestbound -> up left
	else if((updateObject->zmapCellID + GRIDWIDTH-1) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		//so we need to delete down right (Southeast)
		std::list<Object*> temp = *GetAllCellContents(updateObject->zmapCellID - ((GRIDWIDTH-1)*VIEWRANGE));
		FinalList.splice(it, temp);							  

		temp = *GetAllGridContentsListColumnUp(updateObject->zmapCellID - ((GRIDWIDTH-1)*VIEWRANGE) +GRIDWIDTH );//		FinalList.splice(it, temp);
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListRowLeft(updateObject->zmapCellID - ((GRIDWIDTH-1)*VIEWRANGE)-1);//
		FinalList.splice(it, temp);

		for(std::list<Object*>::iterator i = FinalList.begin(); i != FinalList.end(); i++)
		{
			CheckObjectIterationForDestruction((*i),updateObject);
			
		}

		return;
	}

		// SouthWestbound	  -> down left
	else if((updateObject->zmapCellID - (GRIDWIDTH+1)) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		//so we need to delete up right (Northeast)
		std::list<Object*> temp = *GetAllCellContents(updateObject->zmapCellID + (GRIDWIDTH+1)*VIEWRANGE);
		FinalList.splice(it, temp);

		//get the column down 
		temp = *GetAllGridContentsListColumnDown(updateObject->zmapCellID + ((GRIDWIDTH+1)*VIEWRANGE) - GRIDWIDTH );//		FinalList.splice(it, temp);
		FinalList.splice(it, temp);

		//get the row 
		temp = *GetAllGridContentsListRowLeft(updateObject->zmapCellID + ((GRIDWIDTH+1)*VIEWRANGE) -1);//
		FinalList.splice(it, temp);

		for(std::list<Object*>::iterator i = FinalList.begin(); i != FinalList.end(); i++)
		{
			CheckObjectIterationForDestruction((*i),updateObject);
		}

		return;
	}

		// SouthEastbound	-> down right
	else if((updateObject->zmapCellID - (GRIDWIDTH-1)) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		//so we need to delete up left (Northwest)
		std::list<Object*> temp = *GetAllCellContents(updateObject->zmapCellID + ((GRIDWIDTH-1)*VIEWRANGE) );
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListColumnDown(updateObject->zmapCellID + ((GRIDWIDTH-1)*VIEWRANGE) -GRIDWIDTH );//		FinalList.splice(it, temp);
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListRowRight(updateObject->zmapCellID + ((GRIDWIDTH-1)*VIEWRANGE) +1);//
		FinalList.splice(it, temp);
		

		for(std::list<Object*>::iterator i = FinalList.begin(); i != FinalList.end(); i++)
		{
			CheckObjectIterationForDestruction((*i),updateObject);
		}

		return;
	}
	
}

void zmap::CheckObjectIterationForCreation(Object* toBeTested, Object* toBeUpdated)
{
	PlayerObject* us = dynamic_cast<PlayerObject*>(toBeUpdated);

	if(toBeTested->getId() != toBeUpdated->getId())
	{
		//we (toBeUpdated) need to create the following objects
		//if its a player create us for him
		
		if(us)
		{
			gMessageLib->sendCreateObject(toBeTested,us);
		}
				
		PlayerObject* them = dynamic_cast<PlayerObject*> (toBeTested);
		if(them)
		{
			gMessageLib->sendCreateObject(toBeUpdated,them);
		}
	}
}


void zmap::ObjectCreationIteration(std::list<Object*>* FinalList, Object* updateObject)
{
	//at some point we need to throttle ObjectCreates!!!
	//one possibility would be to only send one grid at a time and keep track of up / unup dated Grids

	for(std::list<Object*>::iterator i = FinalList->begin(); i != FinalList->end(); i++)
	{
		CheckObjectIterationForCreation((*i),updateObject);
	}
}

//=============================================================================================
//collect Objects in the new cells
void zmap::UpdateFrontCells(Object* updateObject, uint32 newCell)
{

	//ZMAP Northbound! TODO: Sync with game
	if((updateObject->zmapCellID + GRIDWIDTH) == newCell)
	{
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllGridContentsListRow((updateObject->zmapCellID + (GRIDWIDTH*VIEWRANGE)) + GRIDWIDTH);
		FinalList.splice(it, temp);

		ObjectCreationIteration(&FinalList,updateObject);
		
		return;
	}

	//ZMAP Southbound! TODO: Sync with game
	else if((updateObject->zmapCellID - GRIDWIDTH) == newCell)
	{
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllGridContentsListRow((updateObject->zmapCellID - (GRIDWIDTH*VIEWRANGE)) - GRIDWIDTH);
		FinalList.splice(it, temp);
		ObjectCreationIteration(&FinalList,updateObject);

		return;
	}

			//ZMAP Eastbound! TODO: Sync with game
	else if((updateObject->zmapCellID + 1) == newCell)
	{
	
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllGridContentsListColumn((updateObject->zmapCellID + VIEWRANGE) + 1 );
		FinalList.splice(it, temp);

		ObjectCreationIteration(&FinalList,updateObject);

		return;
	}

		//ZMAP Westbound! TODO: Sync with game
	else if((updateObject->zmapCellID - 1) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllGridContentsListColumn((updateObject->zmapCellID - VIEWRANGE) - 1 );
		FinalList.splice(it, temp);

		ObjectCreationIteration(&FinalList,updateObject);

		return;
	}

	// NorthEastbound
	else if((updateObject->zmapCellID + (GRIDWIDTH+1)) == newCell)
	{
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllCellContents((updateObject->zmapCellID + ((GRIDWIDTH+1)*VIEWRANGE)) + (GRIDWIDTH+1));//
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListColumnDown((updateObject->zmapCellID + ((GRIDWIDTH+1)*VIEWRANGE)) - GRIDWIDTH);//
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListRowLeft((updateObject->zmapCellID + ((GRIDWIDTH+1)*VIEWRANGE)) - 1);//
		FinalList.splice(it, temp);
		
		ObjectCreationIteration(&FinalList,updateObject);

		return;
	}

	// NorthWestbound
	else if((updateObject->zmapCellID + (GRIDWIDTH-1)) == newCell)
	{
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllCellContents((updateObject->zmapCellID + ((GRIDWIDTH-1)*VIEWRANGE)) + (GRIDWIDTH-1));//
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListColumnDown((updateObject->zmapCellID + ((GRIDWIDTH-1)*VIEWRANGE))  + (GRIDWIDTH-1) - GRIDWIDTH);//
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListRowRight((updateObject->zmapCellID + ((GRIDWIDTH-1)*VIEWRANGE))  + (GRIDWIDTH-1) + 1);//
		FinalList.splice(it, temp);
		ObjectCreationIteration(&FinalList,updateObject);

		return;
	}

		// SouthWestbound
	else if((updateObject->zmapCellID - (GRIDWIDTH+1)) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllCellContents((updateObject->zmapCellID - (GRIDWIDTH+1)*VIEWRANGE) - (GRIDWIDTH+1));
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListColumnUp((updateObject->zmapCellID - ((GRIDWIDTH+1)*VIEWRANGE)) - (GRIDWIDTH+1) + GRIDWIDTH);//		FinalList.splice(it, temp);
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListRowRight((updateObject->zmapCellID + ((GRIDWIDTH+1)*VIEWRANGE)) - (GRIDWIDTH+1) + 1);//
		FinalList.splice(it, temp);

		ObjectCreationIteration(&FinalList,updateObject);

		return;
	}

		// SouthEastbound
	else if((updateObject->zmapCellID - (GRIDWIDTH-1)) == newCell)
	{
		
		std::list<Object*> FinalList;
		std::list<Object*>::iterator it = FinalList.end();

		std::list<Object*> temp = *GetAllCellContents((updateObject->zmapCellID - (GRIDWIDTH-1)*VIEWRANGE) - (GRIDWIDTH-1));
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListColumnUp((updateObject->zmapCellID - ((GRIDWIDTH-1)*VIEWRANGE)) - (GRIDWIDTH-1) + GRIDWIDTH);//		FinalList.splice(it, temp);
		FinalList.splice(it, temp);

		temp = *GetAllGridContentsListRowLeft((updateObject->zmapCellID + ((GRIDWIDTH-1)*VIEWRANGE)) - (GRIDWIDTH-1) - 1);//
		FinalList.splice(it, temp);

		ObjectCreationIteration(&FinalList,updateObject);

		return;
	}
}