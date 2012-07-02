/**
 * \file LDAS_XmlParser.cpp
 * \brief Source file for CStringParser class.
 * \author Rick Riedel
 * \date March 8, 2006
 */

#include "stdafx.h"
#include "LDAS_XmlParser.h"


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CStringParser::CStringParser()
{
}

CStringParser::~CStringParser()
{
}

/////////////////////////////////////////////////////
//Strip the header out from processing.  
////////////////////////////////////////////////////
CString CStringParser::StripHeader(CString csXMLFile)
{
	CString csTemp;
	int j=csXMLFile.Find("<?xml");
	if (j<0)
		return csXMLFile;
	else
	{
		j=csXMLFile.Find('>')+1;
		if (j<0)
			return csXMLFile;
		else
		{
			csTemp=csXMLFile.Mid(j);
			return csTemp;
		}
	}
}

/////////////////////////////////////////////////////
//Strip the header+the first tag out from processing.  
////////////////////////////////////////////////////
CString CStringParser::StripFirstTag(CString csXMLFile)
{
	CString csTemp;
	int j=csXMLFile.Find('<');
	if (j<0)
		return csXMLFile;
//
	TCHAR c=csXMLFile.GetAt(j+1);
	if (c=='?')  //means header, use strip header to remove this 
	{
		j=csXMLFile.Find('<',j+3);  //find next occurance
		if (j<0)
			return csXMLFile;

	}

	j=csXMLFile.Find('>',j)+1;
	if (j<0)
		return csXMLFile;
	else
	{
		csTemp=csXMLFile.Mid(j);
		return csTemp;
	}
}

/////////////////////////////////////////////////////
//Strip the last tag from string.  
////////////////////////////////////////////////////
CString CStringParser::StripLastTag(CString csXMLFile)
{
	CString csTemp;
	int j=csXMLFile.ReverseFind('<');
	if (j<0)
		return csXMLFile;
	else
	{
		return csXMLFile.Left(j);
	}

}


/////////////////////////////////////////////////////
//Strip the comments out from processing.  
////////////////////////////////////////////////////
CString CStringParser::StripComments(CString csXMLFile)
{
	CString csTemp;
	int i,j,k;
	csTemp.Empty();
	j=csXMLFile.Find("<?xml");
	if (j<0)
		j=0;
	else
	{
		j=csXMLFile.Find('>')+1;
		if (j<0)
		{
			csTemp.Empty();
			return csTemp;
		}

	}
	k=0;
	i=csXMLFile.Find("<!");
	while (i>=0  && k < MAXCOMMENTS)
	{
		csTemp=csTemp+csXMLFile.Mid(j,i-j);
		k++;
		j=csXMLFile.Find('>',i)+1;
		if (j<0)
		{
			csTemp.Empty();
			return csTemp;
		}
		i=csXMLFile.Find("<!",j);
	}
	i=csXMLFile.GetLength();  //reassign i to end for last cat
	csTemp=csTemp+csXMLFile.Mid(j,i-j);
	csTemp.TrimLeft();
	return csTemp;

}

/*************************************************************/
/* Check for valid xml file
/*************************************************************/

bool CStringParser::CheckValidHeader(CString csXMLFile)
{
//this should probably become more sofisticated
//but for now this is enough
	int i=csXMLFile.Find("<?xml version=\"1.0\"");

	if (i<0)
		return false;
	else
	{
		i=csXMLFile.Find(">",i+15);
		if (i<0)
			return false;
		else
			return true;
	}
}

/*************************************************************/
//GetRoot returns the root element of a valid SNS xml request
//in a CString.  Valid names are 
//
//It returns null if the string does not have a valid xml
//header string <?xml version="1.0"?>
//
/*************************************************************/

CString CStringParser::GetRootName(CString csXMLFile)
{
	int i,j;

	CString csTemp;
	
	i=csXMLFile.Find('<');

	if (i<0)
	{
		csTemp.Empty();
		return csTemp;
	}

	//file header is not consider root so look for ?
	j=0;

	if (csXMLFile.GetAt(i+1)=='?')  //xml header
		i=csXMLFile.Find('<',i+2);

#if PARSECOMMENTS
	while (csXMLFile.GetAt(i+1)=='!' && j<4)  //comment
	{
			i=csXMLFile.Find('<',i+2);
			j++;
	}
#endif
//carry on by looking for "<"
	if (i<0)
	{
		csTemp.Empty();
		return csTemp;
	}

	j=csXMLFile.Find('>',i);
	if (j<0)
	{
		csTemp.Empty();
		return csTemp;
	}
	csTemp=csXMLFile.Mid(i,j-i+1);  //this gets the tag

	csTemp=GetElementName(csTemp);
	
	return csTemp;

}



/*************************************************************/
//GetContent returns the content of the requested element
//an entire element must be passed!
//an empty string if an invalid element is passed
//
/*************************************************************/

CString CStringParser::GetElementContent(CString csElement)
{
	int i,j,k;
	int nameCount=0;  //need because we can have embedded elements
					  //with the same name
	int loopCount=0;
	CString csTemp;
	i=csElement.Find('<');
	if (i<0)
	{
		csTemp.Empty();
		return csTemp;
	}
//check for ? in header or comment if enabled
#if PARSECOMMENTS
	TCHAR c=csElement.GetAt(i+1);
	while ((c=='?' || c=='!'))
	{
		i=csElement.Find('<',i+3);  //find next occurance
		c=csElement.GetAt(i+1);
	}
	if (i<0)
	{
		csTemp.Empty();
		return csTemp;
	}
#endif
	j=csElement.Find('>',i);  
	if (j<0)
	{
		csTemp.Empty();
		return csTemp;
	}
	if (csElement.GetAt(j-1)=='/') //check for empty element
	{
		csTemp.Empty();
		return csTemp;
	}
	if (j>i)
		csTemp=GetElementName(csElement.Mid(i,j-i+1));  //this is the name
	else
	{
		csTemp.Empty();
		return csTemp;
	}
	nameCount++;
	loopCount=0;
	k=j;
	while (loopCount<32)
	{
		i=csElement.Find(csTemp,k);
		if (i<0)
		{
			csTemp.Empty();
			return csTemp;
		}
		if (csElement[i-1]=='/')  //terminator
		{
			nameCount--;
			if (nameCount==0)
			{
				i=i-2;
				return csElement.Mid(j+1,i-j-1);
			}
			else
			{
//look for more dude
				k=i+csTemp.GetLength()+1;
			}
		}
		else if (csElement[i-1]=='<')
		{
//means subelement of the same name has been found
			nameCount++;
			k=i+csTemp.GetLength()+1;
		}
		else
		{
//embedded string of same name
			k=i+csTemp.GetLength();
			loopCount--;
		}
		loopCount++;
	}
	csTemp.Empty();
	return csTemp;
}

/*************************************************************/
//GetElementValue returns the value of the passed Element
//This function assumes that you have any stripped comments
//from the element.  Therefore this is not a truely
//universial Parser routine, but it is not meant to be! 
//The value is the stuff between the element tag
//and the first < belonging to the first subelement
/*************************************************************/

CString CStringParser::GetElementValue(CString csElement)
{
	CString csTemp;
	int i=csElement.Find('>');  //find end of Element Tag
	if (i<0)
	{
		csTemp.Empty();
		return csTemp;
	}

	//carry on by looking for "<"
	int j=csElement.Find('<',i);
	if (j<0)  //this is an error
	{
		csTemp.Empty();
		return csTemp;
	}
	csTemp=csElement.Mid(i+1,j-i-1);
	csTemp.TrimLeft();
	csTemp.TrimRight();
	return csTemp;
}

/*************************************************************/
//GetElementTag returns the tag of the element
//including < and > characters
//an entire element must be passed, 
//also useful for stripping of the root Element of a
//file
/*************************************************************/

CString CStringParser::GetElementTag(CString csElement)
{
	CString csTag;
	int i,j;
//looking for "<"
	i=csElement.Find('<');
	if (i<0)
	{
		csTag.Empty();
		return csTag;
	}
//check for ? in header
/*
#if PARSECOMMENTS
	TCHAR c=csElement->GetAt(i+1);
	while ((c=='?' || c=='!'))
	{
		i=csElement.Find('<',i+3);  //find next occurance
		c=csElement.GetAt(i+1);
	}
#else
	TCHAR c=csElement.GetAt(i+1);
	if (c=='?')
		i=csElement.Find('<',i+3); //look for next <

#endif
*/
	if (i<0)
	{
		csTag.Empty();
		return csTag;
	}
	j=csElement.Find('>',i);
	if (j<0)
	{
		csTag.Empty();
		return csTag;
	}
	return csElement.Mid(i,j-i+1);
}

/*************************************************************/
//NumberOfSubElements returns the number of sub-elements of 
//the requested element
//an entire element must be passed, including < > characters 
// 
/*************************************************************/

int CStringParser::GetNumberOfSubElements(CString csElement)
{
	//only the first instance is sought of csElementName
	//should use overloaded brother if multiple names
	//occur

	
	CString csSubName;
	int i=0,j=0,k=0;
	i=csElement.Find('>',i);  //find element end  tag character
	if (i < 0 || csElement.GetAt(i-1)=='/')  //empty i.e. no sub elements
	{                                //or content
		return 0;
	}


	//carry on by looking for "<"
	while (i>=0  && j< MAXSUBELEMENTS)
	{
		i=csElement.Find('<',i);
		if (csElement.GetAt(i+1)=='/')  //you have hit the end of
			break;						 //the element	
#if PARSECOMMENTS
		while (csElement.GetAt(i+1)=='!'  )  //nice subroutine for 
		{								// ignoring comments	
			i=csElement.Find(">",i+2);
			if (i<0)
				break;
			i=csElement.Find('<',i+1);
			if (i<0)
				break;
		}
#endif
		if (i<0)
			break;
		k=csElement.Find('>',i);
		if (i<0 || k<i+1)
			break;
		csSubName=csElement.Mid(i+1,k-i-1);
		if (csElement.GetAt(csElement.GetLength() -1) != '/')
		{
//look for first space in case there are attributes
			i=csSubName.Find(' ');
			if (i>=0)
				csSubName=csSubName.Mid(0,i);

			i=csElement.Find("</"+csSubName+">",k);  //look for end tag
			if (i<0)
				break;
			i=csElement.Find(">",i);
		}
		j++;
	}
	return j;
}

/*************************************************************/
// The overloading function returns the number of sub elements and fill in the structure
// of the them at the same time.
/*************************************************************/

int CStringParser::GetNumberOfSubElements(CString csElement, CString csSubElementArray[])
{
	//only the first instance is sought of csElementName
	//should use overloaded brother if multiple names
	//occur

	
	CString csSubName;
	int i=0,j=0,k=0;
	i=csElement.Find('>',i);  //find element end  tag character
	if (i < 0 || csElement.GetAt(i-1)=='/')  //empty i.e. no sub elements
	{                                //or content
		return 0;
	}

	int iBegin,iEnd;
//carry on by looking for "<"
	while (i>=0  && j< MAXSUBELEMENTS)
	{
		i=csElement.Find('<',i);
		if (csElement.GetAt(i+1)=='/')  //you have hit the end of
			break;						 //the element	


#if PARSECOMMENTS
		while (csElement.GetAt(i+1)=='!')  //nice subroutine for 
		{								// ignoring comments	
			i=csElement.Find(">",i+2);
			if (i<0)
				break;
			i=csElement.Find('<',i+1);
			if (i<0)
				break;
		}
#endif
		if (i<0)
			break;
		iBegin=i;
		k=csElement.Find('>',i);
		iEnd=k;
		if (i<0 || k<i+1)
			break;
		csSubName=csElement.Mid(i+1,k-i-1);
		if (csElement.GetAt(csElement.GetLength() -1) != '/')
		{
//look for first space in case there are attributes
			i=csSubName.Find(' ');
			if (i>=0)
				csSubName=csSubName.Mid(0,i);

			i=csElement.Find("</"+csSubName+">",k);  //look for end tag
			if (i<0)
				break;
			i=csElement.Find('>',i);
			iEnd=i;
			if (i<0)
				break;
		}
		csSubElementArray[j] = (csElement.Mid(iBegin,iEnd-iBegin+1));
		j++;
	}
	return j;
}


/*************************************************************/
//GetElement returns the element of the location indicated
//an empty string is returned if an error occurs. 
// start at one element number (i.e. first element =1)
/*************************************************************/

CString CStringParser::GetElement(CString csXMLFile, int iElementNumber)
{
	CString csName;
	int i=0,j=0,k=0;
	int nameCount=0;
	int loopCount=0;
	int iBegin,iEnd;
	i=csXMLFile.Find("<"); //check for header
	if (csXMLFile.GetAt(i+1)=='?')  //xml header
		i=5;   //header
	else
		i=0;   //no header

//carry on by looking for "<"
	for (j=0;j<iElementNumber;j++)
	{
		i=csXMLFile.Find('<',i);
#if PARSECOMMENTS
		while (csXMLFile.GetAt(i+1)=='!')  //nice subroutine for 
		{								// ignoring comments	
			i=csXMLFile.Find('>',i+2);
			if (i<0)
				break;
			i=csXMLFile.Find('<',i+1);
			if (i<0)
				break;
		}
#endif
		if (i<0)
			break;
		iBegin=i;
		k=csXMLFile.Find('>',i);
		if (i<0)
			break;
		csName=csXMLFile.Mid(i+1,k-i-1);
		if (csName.GetAt(csName.GetLength() -1) != '/')
		{
//look for first space in case there are attributes
			i=csName.Find(' ');
			if (i>=0)
				csName=csName.Mid(0,i);

/////////////////////////////////////////////////
//we should stick this code in a subroutine
//since it is also used by get element content
////////////////////////////////////////////////

			nameCount++;
			loopCount=0;
			while (loopCount<32)
			{
				i=csXMLFile.Find(csName,k);
				if (i<0)
				{
					csName.Empty();
					return csName;
				}
				if (csXMLFile[i-1]=='/')  //terminator
				{
					nameCount--;
					if (nameCount==0)
					{
						break;
					}
					else
					{
					//look for more dude
						k=i+csName.GetLength()+1;
					}
				}
				else if (csXMLFile[i-1]=='<')
				{
//means subelement of the same name has been found
					nameCount++;
					k=i+csName.GetLength()+1;
				}
				else  //just embedding info
				{
					k=i+csName.GetLength();
					loopCount--;
				}
				loopCount++;
			}


//////////////////////////////////////////////
			
			
			if (i<0)
				break;
			i=csXMLFile.Find('>',i);
			iEnd=i;
			if (i<0)
				break;
		}
		else
		{
			iEnd=k;
			i=k;
		}
	}
	if (i<0)
	{
		csName.Empty();
		return csName;
	}
	else
		return csXMLFile.Mid(iBegin,iEnd-iBegin+1);
}



/*************************************************************/
//GetSubElement returns the subelement of the location indicated
//an empty string is returned if an error occurs. 
//Not, you must pass this function a valid element CString 
/*************************************************************/

CString CStringParser::GetSubElement(CString csElement, int iSubElementNumber)
{
	CString csSubName;
	int i=0,j=0,k=0;
	int iBegin,iEnd;
	i=csElement.Find('>',i);  //find element end  tag character
	if (i < 0 || csElement.GetAt(i-1)=='/')  //empty i.e. no sub elements
	{                                //or content
		csSubName.Empty();
		return csSubName;
	}
//carry on by looking for "<"
	for (j=0;j<iSubElementNumber;j++)
	{
		i=csElement.Find('<',i);
		if (csElement.GetAt(i+1)=='/')  //you have hit the end of
			break;						 //the element	
#if PARSECOMMENTS
		while (csElement.GetAt(i+1)=='!')  //nice subroutine for 
		{								// ignoring comments	
			i=csElement.Find(">",i+2);
			if (i<0)
				break;
			i=csElement.Find('<',i+1);
			if (i<0)
				break;
		}
#endif
		if (i<0)
			break;
		iBegin=i;
		k=csElement.Find('>',i+1);
		if (i<0)
			break;
		csSubName=csElement.Mid(i+1,k-i-1);

		if (csElement.GetAt(csElement.GetLength() -1) != '/')  //means empty subelement
		{
//look for first space in case there are attributes
			i=csSubName.Find(' ');
			if (i>=0)
				csSubName=csSubName.Mid(0,i);

			i=csElement.Find('/'+csSubName+">",k);  //look for end tag
			if (i<0)
				break;
			i=csElement.Find('>',i);
			iEnd=i;
			if (i<0)
				break;
		}
	}
	if (i<0)
	{
		csSubName.Empty();
		return csSubName;
	}
	else
		return csElement.Mid(iBegin,iEnd-iBegin+1);
}


//**********************************************************
//Pass an element, or element tag to this function to
//get its name but you must include the < or else!
//**********************************************************

CString CStringParser::GetElementName(CString csElement)
{
	CString csName;
	int i,j, k;
//looking for "<"
		i=csElement.Find('<');
		if (i<0)
		{
			csName.Empty();
			return csName;
		}
		j =csElement.Find('>',i);
		if (j<0 || j<i+1)		//no end tag
		{
			csName.Empty();
			return csName;
		}
		csName = csElement.Mid(i+1,j-i-1);
//Looking for a space or a '/'
		k=csName.Find(' ');  //look for a space
		if (k<0)  //no space there either /> 
		{
						
			i=csName.GetLength();
			if (csName.GetAt(i-1)=='/')  //might be empty element
			csName=csName.Mid(0,i-1);
		}
		else
		{
			csName=csName.Mid(0,k);
			csName.TrimRight();
		}
		return csName;
}

//**********************************************************
//must pass this function an element.
//
//**********************************************************

ELE_STRUCT CStringParser::GetElementStructure(CString csElement)
{
	CString csTemp;

	ELE_STRUCT eStructure;

	eStructure.csName=GetElementName(csElement);
	eStructure.csValue=GetElementValue(csElement);
	csTemp=GetElementTag(csElement);
	eStructure.uiNumberOfAttributes=GetNumberOfAttributes(csTemp,eStructure.sAttribute);

	eStructure.uiNumberOfSubElements=GetNumberOfSubElements(csElement, eStructure.csSubElement);


	return eStructure;

}

//**********************************************************
//do by looking for =" strings
//using the overloaded find with starting index makes this 
//function trivial.  Should pass a tag to this
//but should work with tag content also
//***********************************************************

int CStringParser::GetNumberOfAttributes(CString csTag)

{
	int i=0,j=0;
//	CString csTemp;
	if (csTag.IsEmpty())                                                                                                                                                                                                                                    
                       
		return -1;
//carry on by looking for '="'
	while (i>=0  && j< MAXATTRIBUTES)    //note we allow only 7 in elementstruct
	{
	
			i=csTag.Find("=\"",i);
			i=csTag.Find("\"",i+2);
			if (i<0)
			{
				break;
			}
			else
			{
				i++;
				j++;
			}
	
	}

	return j;
}
//***********************************************************
// This overloading function will not only find the number of 
// attributes but also fills the attribute structure
//***********************************************************

int CStringParser::GetNumberOfAttributes(CString csTag, ATT_STRUCT csAttribute[])

{
	int i=0,j=0,k=0, n=0;
	if (csTag.IsEmpty()) 
	{
		return -1;
	}
//carry on by looking for '="'
	while (n< MAXATTRIBUTES)    //note we allow only 7 in elementstruct
	{
	
			i=csTag.Find(' ',k);	
		
			if (i <0)
				break;

			j=csTag.Find("=\"",i);
			if ( j<0)
				break;
		
			k=csTag.Find('\"',j+2);  //k is the end of the attribute

			if (k<0)
				break;

			csAttribute[n].csName = csTag.Mid(i+1,j-i-1);
			csAttribute[n].csName.TrimLeft();
		    csAttribute[n].csValue=csTag.Mid(j+2,k-j-2);
			n++;
	
	}

	return n;
}


//**********************************************************
//you must pass a tag CString to this function
//
//**********************************************************

bool CStringParser::GetAttributeStructure(CString csTag, PATT_STRUCT pAttribute, int iAttributeNumber)
{
	int i,j,k,n;
	if (iAttributeNumber <=0  || iAttributeNumber > MAXATTRIBUTES)
	{
		return false;
	}

//carry on by looking for "=""
	i=0;
	j=0;
	k=0;
	
//this technique slides through the string using the overloaded 
//Find method
//
	for (n=0;n<iAttributeNumber;n++) 
	{
		i=csTag.Find(' ',k);	
		if (i <0)
		{
			return false;
		}

		j=csTag.Find("=\"",i);
		if (j <0)
		{
			return false;
		}
		
		
		k=csTag.Find('\"',j+2);  //k is the end of the attribute
		if (k<0)
		{
			return false;
		} //no attribute
		
	}
		
	pAttribute->csName=csTag.Mid(i+1,j-i-1);
	pAttribute->csValue=csTag.Mid(j+2,k-j-2);
//now trim white characters from csName
	pAttribute->csName.TrimLeft();
		

	return true;
	

}

//**********************************************************
//you must pass a tag CString to this function
//
//**********************************************************

CString CStringParser::GetAttributeValue(CString csTag, CString csName)
{
	int i=0,j=0;
	CString csTemp;
	csTemp.Empty();
	if (csName.IsEmpty())
	{
		return csTemp;
	}

//this technique slides through the string using the overloaded 
//Find method
//
	while (i>=0)
	{
		i=csTag.Find(csName,i);
		if (i<0)
		{
			return csTemp;
		}
		if (csTag.GetAt(i+csName.GetLength()) == '=')
		{
			break;
		}
		else
		{
			i=i+csName.GetLength();
		}
	}
	i=csTag.Find("=\"",i);
	if (i<0)
	{
		return csTemp;
	}
	j=csTag.Find("\"",i+2);
	if (j<0)
	{
		return csTemp;
	}

	csTemp=csTag.Mid(i+2,j-i-2);
		

	return csTemp;
	

}


/*************************************************************/
// This function replaces the value (content) of the listed element
// with the text.  Returns new element or empty string if error...
/*************************************************************/

CString CStringParser::Replace(CString element, CString tagname, CString text)
{
	CString csLeftHack;
	CString csTemp;
	int i,j;
	i=tagname.Find('<');
	if (i>=0)
	{
		//formating error...
		csTemp.Empty();
		return csTemp;
	}
	csTemp="<"+tagname;
	i=element.Find(csTemp);
	if (i<0)
	{
		//formating error...
		csTemp.Empty();
		return csTemp;
	}
	i=element.Find('>',i+2);
	if (i<0)
	{
		//formating error...
		csTemp.Empty();
		return csTemp;
	}
	i=i+1;

	csTemp="</"+tagname;
	j=element.Find(csTemp,i);
	if (j<0)
	{
		//formating error...
		csTemp.Empty();
		return csTemp;
	}
	csLeftHack = element.Left(i);
	csTemp=element.Right(element.GetLength()-j);
//okay make the string that will go here......
	csTemp=csLeftHack + text + csTemp;
	return csTemp;
}
