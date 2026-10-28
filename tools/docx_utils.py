# -*- coding: utf-8 -*-
import os
import docx
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_ALIGN_VERTICAL
from docx.oxml import parse_xml, OxmlElement
from docx.oxml.ns import nsdecls, qn

def set_run_font(run, font_name="微软雅黑", size_pt=10.5, bold=False, color_rgb=(51,51,51)):
    run.font.name = font_name
    run._element.rPr.rFonts.set(qn('w:eastAsia'), font_name)
    run.font.size = Pt(size_pt)
    run.font.bold = bold
    run.font.color.rgb = RGBColor(*color_rgb)

def add_styled_heading(doc, text, level):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(14)
    p.paragraph_format.space_after = Pt(6)
    p.paragraph_format.line_spacing = 1.2
    run = p.add_run(text)
    if level == 1:
        set_run_font(run, "微软雅黑", 16, True, (31, 73, 125)) # Deep Navy
        # Add bottom border under Heading 1
        pBdr = parse_xml(r'<w:pBdr xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">'
                         r'<w:bottom w:val="single" w:sz="12" w:space="4" w:color="1F497D"/>'
                         r'</w:pBdr>')
        p._element.get_or_add_pPr().append(pBdr)
    elif level == 2:
        set_run_font(run, "微软雅黑", 13, True, (46, 117, 182)) # Slate Blue
    elif level == 3:
        set_run_font(run, "微软雅黑", 11, True, (60, 60, 60))
    return p

def add_body_p(doc, text="", space_after=4):
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(space_after)
    p.paragraph_format.line_spacing = 1.25
    if text:
        run = p.add_run(text)
        set_run_font(run, "微软雅黑", 10.5, False, (51, 51, 51))
    return p

def add_callout_box(doc, title, text, box_type="answer"):
    """
    box_type: 'answer' (green/blue accent) or 'tip' (amber accent) or 'quote'
    """
    tbl = doc.add_table(rows=1, cols=1)
    tbl.alignment = WD_TABLE_ALIGNMENT.CENTER
    tbl.autofit = False
    
    cell = tbl.cell(0, 0)
    cell.width = Inches(6.5)
    
    # Border & shading
    if box_type == "answer":
        bg_color = "F2F7FA" # Light ice blue
        border_color = "1F497D" # Navy
        icon = "💡【面试 1 分钟精炼答辩模板】"
    elif box_type == "trap":
        bg_color = "FFF9F2" # Light orange
        border_color = "C55A11" # Amber
        icon = "⚠️【考官追问与致命防守陷阱】"
    else:
        bg_color = "F5F5F5"
        border_color = "595959"
        icon = "📌【核心概念解析】"
        
    shading = parse_xml(f'<w:shd {nsdecls("w")} w:fill="{bg_color}"/>')
    cell._element.get_or_add_tcPr().append(shading)
    
    borders = parse_xml(f'''
        <w:tcBorders {nsdecls("w")}>
            <w:top w:val="none"/>
            <w:left w:val="single" w:sz="24" w:space="0" w:color="{border_color}"/>
            <w:bottom w:val="none"/>
            <w:right w:val="none"/>
        </w:tcBorders>
    ''')
    cell._element.get_or_add_tcPr().append(borders)
    
    # Margins
    mar = parse_xml(r'<w:tcMar xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">'
                    r'<w:top w:w="120" w:type="dxa"/>'
                    r'<w:left w:w="180" w:type="dxa"/>'
                    r'<w:bottom w:w="120" w:type="dxa"/>'
                    r'<w:right w:w="180" w:type="dxa"/>'
                    r'</w:tcMar>')
    cell._element.get_or_add_tcPr().append(mar)
    
    # Title paragraph
    p0 = cell.paragraphs[0]
    p0.paragraph_format.space_after = Pt(3)
    r0 = p0.add_run(f"{icon} {title}")
    set_run_font(r0, "微软雅黑", 10.5, True, (31, 73, 125) if box_type != "trap" else (197, 90, 17))
    
    # Content paragraph
    p1 = cell.add_paragraph()
    p1.paragraph_format.space_after = Pt(2)
    p1.paragraph_format.line_spacing = 1.25
    r1 = p1.add_run(text)
    set_run_font(r1, "微软雅黑", 10, False, (51, 51, 51))
    
    # Spacer
    doc.add_paragraph().paragraph_format.space_after = Pt(4)

def format_table_header(row, col_widths, headers, bg_color="1F497D"):
    for idx, cell in enumerate(row.cells):
        cell.width = col_widths[idx]
        shd = parse_xml(f'<w:shd {nsdecls("w")} w:fill="{bg_color}"/>')
        cell._element.get_or_add_tcPr().append(shd)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(4)
        p.paragraph_format.space_after = Pt(4)
        r = p.add_run(headers[idx])
        set_run_font(r, "微软雅黑", 10, True, (255, 255, 255))

def format_table_row(row, col_widths, values, is_even=False):
    bg_color = "F9FAFC" if is_even else "FFFFFF"
    for idx, cell in enumerate(row.cells):
        cell.width = col_widths[idx]
        shd = parse_xml(f'<w:shd {nsdecls("w")} w:fill="{bg_color}"/>')
        cell._element.get_or_add_tcPr().append(shd)
        p = cell.paragraphs[0]
        p.paragraph_format.space_before = Pt(3)
        p.paragraph_format.space_after = Pt(3)
        p.paragraph_format.line_spacing = 1.2
        r = p.add_run(values[idx])
        set_run_font(r, "微软雅黑", 9.5, False, (51, 51, 51))

print("Helper functions ready")
