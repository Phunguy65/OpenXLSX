//
// Created by Troldal on 03/09/16.
//

#include "../headers/XLWorksheet.h"
#include "../headers/XLWorkbook.h"
#include "../headers/XLCell.h"
#include "../headers/XLCellValue.h"
#include "../headers/XLCellRange.h"
#include "../headers/XLRow.h"
#include "../headers/XLColumn.h"
#include "../headers/XLException.h"
#include "../headers/XLTokenizer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace std;
using namespace OpenXLSX;

/**
 * @details The constructor initializes the member variables and calls the loadXMLData from the
 * XLAbstractXMLFile base class.
 */
XLWorksheet::XLWorksheet(XLWorkbook &parent,
                         const std::string &name,
                         const std::string &filePath,
                         const std::string &xmlData)

    : XLSheet(parent, name, filePath, xmlData),
      m_dimensionNode(XMLNode()),
      m_sheetDataNode(XMLNode()),
      m_columnsNode(XMLNode()),
      m_sheetViewsNode(XMLNode()),
      m_parentWorkbook(parent),
      m_rows(),
      m_columns(),
      m_firstCell(1, 1),
      m_lastCell(1, 1),
      m_maxColumn(0)
{

    // Call the 'LoadXMLData' method in the XLAbstractXMLFile base class
    ParseXMLData();
}

/**
 * @details This function reads the .xml file for the worksheet and populates the data into the internal
 * datastructure of the class. This function is not called directly, but is called via the loadXMLData
 * function in the base class.
 */
bool XLWorksheet::ParseXMLData()
{

    // Get pointers to key nodes in the XML document.
    InitDimensionNode();
    InitSheetViewsNode();
    InitSheetDataNode();
    InitColumnsNode();

    // Read the dimensions of the Sheet and set data members accordingly.
    string dimensions = DimensionNode().attribute("ref").value();
    SetFirstCell(XLCellReference("A1"));
    if (dimensions.find(":") == string::npos) SetLastCell(XLCellReference("A1"));
    else SetLastCell(XLCellReference(dimensions.substr(dimensions.find(":") + 1)));

    // If Column properties are grouped, divide them into properties for individual Columns.
    if (m_columnsNode.type() != pugi::node_null) {
        auto currentNode = ColumnsNode().first_child();
        while (currentNode != nullptr) {
            int min = stoi(currentNode.attribute("min").value());
            int max = stoi(currentNode.attribute("max").value());
            m_maxColumn = max;
            if (min != max) {
                currentNode.attribute("min").set_value(max);
                for (int i = min; i < max; i++) {
                    auto newnode = ColumnsNode().insert_child_before("col", currentNode);
                    auto attr = currentNode.first_attribute();
                    while (attr) {
                        newnode.append_attribute(attr.name()) = attr.value();
                        attr = attr.next_attribute();
                    }
                    newnode.attribute("min") = i;
                    newnode.attribute("max") = i;
                }
            }
            currentNode = currentNode.next_sibling();
        }
    }

    // Store all Column nodes in the m_columns vector.
    if (m_columnsNode.type() != pugi::node_null) {
        auto currentColumn = ColumnsNode().first_child();
        while (currentColumn) {
            m_columns.emplace(currentColumn.attribute("min").as_ullong() - 1, XLColumn(*this, currentColumn));
            currentColumn = currentColumn.next_sibling();
        }
    }

    // Store all Row nodes in the m_rows vector. The XLRow constructor will initialize the cells objects
    for (auto &currentRow : SheetDataNode().children())
        m_rows.emplace(stoul(currentRow.attribute("r").value()) - 1, XLRow(*this, currentRow));

    return true;
}

/**
 * @details Creates an identical clone of the worksheet. All references internally in the spreadsheet are
 * handled automatically by the clone function.
 */
XLWorksheet *XLWorksheet::Clone(const std::string &newName)
{
    ParentWorkbook()->CloneWorksheet(Name(), newName);
    return ParentWorkbook()->Worksheet(newName);
}

/**
 * @details Get te cell with the given cell reference. This is a convenience function, which calls the
 * cell(rowNumber, columnNumber) function.
 */
XLCell *XLWorksheet::Cell(const XLCellReference &ref)
{
    return Cell(ref.Row(), ref.Column());
}

/**
 * @details
 */
const XLCell *XLWorksheet::Cell(const XLCellReference &ref) const
{
    return Cell(ref.Row(), ref.Column());
}

/**
 * @details Get the cell with the given cell address. This is a convenience function, which calls the
 * cell(rowNumber, columnNumber) function.
 */
XLCell *XLWorksheet::Cell(const std::string &address)
{
    return Cell(XLCellReference(address));
}

/**
 * @details
 */
const XLCell *XLWorksheet::Cell(const std::string &address) const
{
    return Cell(XLCellReference(address));
}

/**
 * @details This function returns a pointer to an XLCell object in the worksheet. This particular overload
 * also serves as the main function, called by the other overloads.
 */
XLCell *XLWorksheet::Cell(unsigned long rowNumber,
                          unsigned int columnNumber)
{
    // If the requested Cell is outside the current Sheet Range, reset the m_lastCell Property accordingly.
    if (columnNumber > ColumnCount() || rowNumber > RowCount()) {
        if (columnNumber > ColumnCount()) m_lastCell.SetColumn(columnNumber);
        if (rowNumber > RowCount()) m_lastCell.SetRow(rowNumber);

        // Reset the dimension node to reflect the full Range of the current Sheet.
        DimensionNode().attribute("ref").set_value(string(FirstCell().Address() + ":" + LastCell().Address()).c_str());
    }

    // Return a pointer to the requested XLCell object.
    return Row(rowNumber)->Cell(columnNumber);
}

/**
 * @details
 * @throw XLException if rowNumber exceeds rowCount
 */
const XLCell *XLWorksheet::Cell(unsigned long rowNumber,
                                 unsigned int columnNumber) const
{
    if (rowNumber > RowCount()) throw XLException("Row " + to_string(rowNumber) + " does not exist!");
    else return Row(rowNumber)->Cell(columnNumber);
}

/**
 * @details Get an XLCellRange object, encompassing all valid cells in the worksheet. This is a convenience function
 * that calls a different function overload.
 */
XLCellRange XLWorksheet::Range()
{
    return Range(FirstCell(), LastCell());
}

/**
 * @brief
 * @return
 * @todo The returned object is not const.
 */
const XLCellRange XLWorksheet::Range() const
{
    return Range(FirstCell(), LastCell());
}

/**
 * @details Get an XLCellRange object with the given coordinates.
 */
XLCellRange XLWorksheet::Range(const XLCellReference &topLeft,
                               const XLCellReference &bottomRight)
{
    // Set the last Cell to some value, in order to create all objects in Range.
    if (Cell(bottomRight)->ValueType() == XLValueType::Empty) Cell(bottomRight)->Value()->Clear();

    return XLCellRange(*this, topLeft, bottomRight);
}

/**
 * @details
 */
const XLCellRange XLWorksheet::Range(const XLCellReference &topLeft,
                                     const XLCellReference &bottomRight) const
{
    // Set the last Cell to some ValueAsString, in order to create all objects in Range.
    //if (Cell(bottomRight)->CellType() == XLCellType::Empty) Cell(bottomRight)->SetEmptyValue();

    return XLCellRange(*this, topLeft, bottomRight);
}

/**
 * @details Get the XLRow object corresponding to the given row number. In the XML file, all cell data are stored under
 * the corresponding row, and all rows have to be ordered in ascending order. If a row have no data, there may not be a
 * node for that row. In RapidXLSX,the vector with all rows are initialized to a fixed size (the maximum size),
 * but most elements will be nullptr.
 */
XLRow *XLWorksheet::Row(unsigned long rowNumber)
{

    // Create result object and initialize to nullptr.
    XLRow *result = nullptr;

    auto iter = Rows()->find(rowNumber - 1);

    // Retrieve the Row node in the m_rows vector. If it doesn't exist, nullptr will be returned.
    if (iter != Rows()->end()) {
        result = &iter->second;
    }
    // If the node does not exist, create and insert it. Otherwise return the existing object.
    else {
        //Rows()->insert({rowNumber - 1, XLRow::CreateRow(*this, rowNumber)});
        XLRow::CreateRow(*this, rowNumber);
        result = &Rows()->at(rowNumber - 1);
    }

    return result;
}

/**
 * @details
 */
const XLRow *XLWorksheet::Row(unsigned long rowNumber) const
{
    if (rowNumber >= m_rows.size()) throw XLException("Row number " + to_string(rowNumber) + " does not exist");
    return &Rows()->at(rowNumber - 1); // vector is 0-based, Excel is 1-based; therefore rowNumber-1.
}

/**
 * @details Get the XLColumn object corresponding to the given column number. In the underlying XML data structure,
 * column nodes do not hold any cell data. Columns are used solely to hold data regarding column formatting.
 */
XLColumn *XLWorksheet::Column(unsigned int columnNumber)
{

    // If no columns exists, create the <cols> node in the XML document.
    if (!m_columnsNode) m_columnsNode = XmlDocument()->root().insert_child_before("cols", SheetDataNode());

    // Create result object and initialize to nullptr.
    XLColumn *result = nullptr;

    // Retrieve the Column node in the m_columns vector. If it doesn't exist, nullptr will be returned.
    XLColumn *colNode = &m_columns.at(columnNumber - 1); // vector is 0-based;

    // If the node does not exist, create and insert it.
    if (colNode == nullptr) {
        // Create the node.
        XMLNode nodeColumn;

        // Insert the newly created Column node in the right place in the XML structure.
        if (!ColumnsNode().first_child() || columnNumber >= m_maxColumn) {
            // If there are no Column nodes, or the requested Column number exceed the current maximum, append the the node
            // after the existing Column nodes.
            nodeColumn = ColumnsNode().append_child("col");
        }
        else {
            //Otherwise, search the Column nodes vector for the next node and insert there.
            auto index = columnNumber - 1; // vector is 0-based, Excel is 1-based; therefore columnNumber-1.
            XLColumn *col = &m_columns.at(index);
            while (col == nullptr) col = &m_columns.at(index++);
            nodeColumn = ColumnsNode().insert_child_before("col", col->ColumnNode());
        }

        nodeColumn.append_attribute("min") = columnNumber;
        nodeColumn.append_attribute("max") = columnNumber;
        nodeColumn.append_attribute("width") = 10;
        nodeColumn.append_attribute("customWidth") = 1;

        // Insert the new Row node in the Row nodes vector.
        m_columns.emplace(columnNumber - 1, XLColumn(*this, nodeColumn));
        result = &m_columns.at(columnNumber - 1);
    }
    else {
        result = colNode;
    }

    if (columnNumber > m_maxColumn) m_maxColumn = columnNumber;

    return result;
}

/**
 * @details
 */
const XLColumn *XLWorksheet::Column(unsigned int columnNumber) const
{
    if (columnNumber >= m_columns.size())
        throw XLException("Column number " + to_string(columnNumber) + " does not exist");
    return &m_columns.at(columnNumber - 1);
}

/**
 * @details
 */
XLRows *XLWorksheet::Rows()
{
    return &m_rows;
}

/**
 * @brief
 * @return
 */
const XLRows *XLWorksheet::Rows() const
{
    return &m_rows;
}

/**
 * @details
 */
XLColumns *XLWorksheet::Columns()
{
    return &m_columns;
}

/**
 * @brief
 * @return
 */
const XLColumns *XLWorksheet::Columns() const
{
    return &m_columns;
}

/**
 * @details
 */
XMLNode XLWorksheet::DimensionNode()
{
    if (!m_dimensionNode) throw XLException("The <dimension> node does not exist in " + FilePath());
    return m_dimensionNode;
}

/**
 * @details
 * @todo Instead of throwing an exception, consider creating a dimension node.
 */
const XMLNode XLWorksheet::DimensionNode() const
{
    if (!m_dimensionNode) throw XLException("The <dimension> node does not exist in " + FilePath());
    return m_dimensionNode;
}

/**
 * @details
 */
void XLWorksheet::InitDimensionNode()
{
    m_dimensionNode = XmlDocument()->first_child().child("dimension");
}

/**
 * @details
 */
XMLNode XLWorksheet::SheetDataNode()
{
    if (!m_sheetDataNode) throw XLException("The <sheetData> node does not exist in " + FilePath());
    return m_sheetDataNode;
}

/**
 * @details
 */
const XMLNode XLWorksheet::SheetDataNode() const
{
    if (!m_sheetDataNode) throw XLException("The <sheetData> node does not exist in " + FilePath());
    return m_sheetDataNode;
}

/**
 * @details
 */
void XLWorksheet::InitSheetDataNode()
{
    m_sheetDataNode = XmlDocument()->first_child().child("sheetData");
}

/**
 * @details Returns the m_columnsNode member variable as a reference.
 * @note The m_columnsNode member variable must have a value. Otherwise, there is something wrong with the XML file,
 * or the initialization of the object.
 * @throw An XLException object with a description of the error.
 */
XMLNode XLWorksheet::ColumnsNode()
{
    if (!m_columnsNode) throw XLException("The <cols> node does not exist in " + FilePath());
    return m_columnsNode;
}

/**
 * @details Returns the m_columnsNode member variable as a const reference.
 * @note The m_columnsNode member variable must have a value. Otherwise, there is something wrong with the XML file,
 * or the initialization of the object.
 * @throw An XLException object with a description of the error.
 */
const XMLNode XLWorksheet::ColumnsNode() const
{
    if (!m_columnsNode) throw XLException("The <cols> node does not exist in " + FilePath());
    return m_columnsNode;
}

/**
 * @details Assigns the address of the node parameter to the m_columnsNode member variable.
 * @note This member function is only intended to be used during object initialization.
 */
void XLWorksheet::InitColumnsNode()
{
    m_columnsNode = XmlDocument()->first_child().child("cols");
}

/**
 * @details Returns the m_sheetViewsNode member variable as a reference.
 * @note The m_sheetViewsNode member variable must have a value. Otherwise, there is something wrong with the XML file,
 * or the initialization of the object.
 * @exception Throws an XLException if the sheetViews node does not exist in underlying XML file.
 */
XMLNode XLWorksheet::SheetViewsNode()
{
    if (!m_sheetViewsNode) throw XLException("The <sheetViews> node does not exist in " + FilePath());
    return m_sheetViewsNode;
}

/**
 * @details Returns the m_sheetViewsNode member variable as a const reference.
 * @note The m_sheetViewsNode member variable must have a value. Otherwise, there is something wrong with the XML file,
 * or the initialization of the object.
 * @exception Throws an XLException if the sheetViews node does not exist in underlying XML file.
 * @todo Instead of throwing an exception, consider creating a sheetViews node.
 */
const XMLNode XLWorksheet::SheetViewsNode() const
{
    if (!m_sheetViewsNode) throw XLException("The <sheetViews> node does not exist in " + FilePath());
    return m_sheetViewsNode;
}

/**
 * @details If the m_sheetViewsNode is null, assign the address of the node parameter to it.
 * @note This member function is only intended to be used during object initialization.
 * @pre The underlying XML file must be existing and valid.
 * @post The m_sheetViewsNode variable must be in a valid state. nullptr is also a valid state, as sheetViews node
 * is not required in the XML file.
 * @todo Consider creating the sheetViews node if it doesn't exist.
 */
void XLWorksheet::InitSheetViewsNode()
{
    // Only set the variable if it is null.
    if (!m_sheetViewsNode) m_sheetViewsNode = XmlDocument()->first_child().child("sheetViews");
}

/**
 * @details Returns the value of the m_firstCell member variable.
 * @pre The m_lastCell member variable must have a valid value.
 * @post The object must remain unmodified.
 */
XLCellReference XLWorksheet::FirstCell() const noexcept
{
    return m_firstCell;
}

/**
 * @details Returns the value of the m_lastCell member variable.
 * @pre The m_lastCell member variable must have a valid value.
 * @post The object must remain unmodified.
 */
XLCellReference XLWorksheet::LastCell() const noexcept
{
    return m_lastCell;
}

/**
 * @details Assigns the cellRef parameter to the m_firstCell member variable. This is an internal member function, which
 * should only be used if the occupied part of the worksheet changes dimensions. In most cases, m_firstCell will
 * refer to cell 'A1', but in some cases it may not.
 * @pre The cellRef parameter must represent a valid cell address.
 * @post The value of the m_lastCell member variable is equal to that of the cellRef parameter.
 */
void XLWorksheet::SetFirstCell(const XLCellReference &cellRef) noexcept
{
    m_firstCell = cellRef;
}

/**
 * @details Assigns the cellRef parameter to the m_lastCell member variable. This is an internal member function, which
 * should only be used if the occupied part of the worksheet changes dimensions.
 * @pre The cellRef parameter must represent a valid cell address.
 * @post The value of the m_lastCell member variable is equal to that of the cellRef parameter.
 */
void XLWorksheet::SetLastCell(const XLCellReference &cellRef) noexcept
{
    m_lastCell = cellRef;
}

/**
 * @details Returns the column() value from the last (bottom right) cell of the worksheet.
 * @pre LastCell() must return a valid XLCellReference object, which accurately represents the worksheet size.
 * @post Object must remain unmodified.
 */
unsigned int XLWorksheet::ColumnCount() const noexcept
{
    return LastCell().Column();
}

/**
 * @details Returns the row() value from the last (bottom right) cell of the worksheet.
 * @pre LastCell() must return a valid XLCellReference object, which accurately represents the worksheet size.
 * @post Object must remain unmodified.
 */
unsigned long XLWorksheet::RowCount() const noexcept
{
    return LastCell().Row();
}

/**
 * @details
 */
std::string XLWorksheet::NewSheetXmlData()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\""
           " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
           " xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" mc:Ignorable=\"x14ac\""
           " xmlns:x14ac=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/ac\">"
           "<dimension ref=\"A1\"/>"
           "<sheetViews>"
           "<sheetView workbookViewId=\"0\"/>"
           "</sheetViews>"
           "<sheetFormatPr baseColWidth=\"10\" defaultRowHeight=\"16\" x14ac:dyDescent=\"0.2\"/>"
           "<sheetData/>"
           "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
           "</worksheet>";
}

/**
 * @details
 */
void XLWorksheet::Export(const std::string &fileName, char decimal, char delimiter)
{
    ofstream file(fileName);
    string token;
    char oldDecimal;

    if (decimal == ',') oldDecimal = '.';
    else oldDecimal = ',';

    for (unsigned long row = 1; row <= RowCount(); ++row) {
        for (unsigned int column = 1; column <= ColumnCount(); ++column) {
            token = Cell(row, column)->Value()->AsString();
            replace(token.begin(), token.end(), oldDecimal, decimal);
            file << token << delimiter;
        }
        file << "\n";
    }

    file.close();
}

/**
 * @details
 */
void XLWorksheet::Import(const std::string &fileName, const string &delimiter)
{
    ifstream file (fileName);
    string line;
    unsigned long row = 1;
    XLTokenizer tokenizer("", delimiter);
    while (getline(file, line)) {
        tokenizer.SetString(line);
        unsigned int column = 1;
        for (auto &iter : tokenizer.Split()) {
            if (iter.IsInteger()) Cell(row, column)->Value()->Set(iter.AsInteger());
            if (iter.IsFloat()) Cell(row, column)->Value()->Set(iter.AsFloat());
            if (iter.IsString()) Cell(row, column)->Value()->Set(iter.AsString());
            if (iter.IsBoolean()) {
                if (iter.AsBoolean() == true) Cell(row, column)->Value()->Set(true);
                else Cell(row, column)->Value()->Set(false);
            }
            column++;
        }
        row++;
    }

    file.close();
}

string XLWorksheet::GetXmlData() const
{
    ostringstream ostr;
    m_xmlDocument->save(ostr);
    return ostr.str();
}