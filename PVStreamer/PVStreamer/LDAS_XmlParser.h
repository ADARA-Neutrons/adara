/**
 * \file LDAS_XmlParser.h
 * \brief Header file for CStringParser class.
 * \author Rick Riedel
 * \date March 8, 2006
 */

#if !defined(AFX_STRINGPARSER_H__BC8671C0_E75D_11D5_8447_0002E315824B__INCLUDED_)
#define AFX_STRINGPARSER_H__BC8671C0_E75D_11D5_8447_0002E315824B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define MAXATTRIBUTES 15
#define MAXSUBELEMENTS 100
#define MAXCOMMENTS 100

typedef struct _ATT_STRUCT{
	CString csName;
	CString csValue;
} ATT_STRUCT, *PATT_STRUCT;

typedef struct _ELE_STRUCT{
	CString csName;
	CString csValue;
	unsigned int uiNumberOfAttributes;
	ATT_STRUCT sAttribute[MAXATTRIBUTES];
	unsigned int uiNumberOfSubElements;
	CString csSubElement[MAXSUBELEMENTS];
} ELE_STRUCT, *PELE_STRUCT;



class CStringParser  
{
public:
	CStringParser();
	virtual ~CStringParser();
	CString GetRootName(CString);
	CString GetElementContent(CString);
	CString GetElementValue(CString);
	CString GetElementName(CString);
	CString GetElementTag(CString);
	CString StripHeader(CString);
	ELE_STRUCT GetElementStructure(CString);
	int     GetNumberOfAttributes(CString);
	int     GetNumberOfAttributes(CString, ATT_STRUCT []);
	bool GetAttributeStructure(CString, PATT_STRUCT, int);  //gets the nth attribute
	CString GetAttributeValue(CString, CString);  //gets the value
	int     GetNumberOfSubElements(CString);
	int GetNumberOfSubElements(CString, CString []);
	CString GetSubElement(CString, int);
	CString GetElement(CString,int);
	bool CheckValidHeader(CString);
	CString StripComments(CString);
	CString StripFirstTag(CString);
	CString StripLastTag(CString);
	CString Replace(CString element, CString tagname,CString replacetext); 

private:
//	CString m_csBuffer;

};

#endif // !defined(AFX_STRINGPARSER_H__BC8671C0_E75D_11D5_8447_0002E315824B__INCLUDED_)


