
/*
 * The olsr.org Optimized Link-State Routing daemon (olsrd)
 * Copyright (c) 2004, Thomas Lopatic (thomas@lopatic.de)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#if !defined(AFX_MYDIALOG4_H__1A381668_A36B_4C51_9B79_643BC2A59D88__INCLUDED_)
#define AFX_MYDIALOG4_H__1A381668_A36B_4C51_9B79_643BC2A59D88__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif

class MyDialog4:public CDialog {
public:
  MyDialog4(CWnd * pParent = NULL);

  BOOL Create(CWnd * Parent);

  void AddRoute(unsigned int, unsigned int, int, char *);
  void DeleteRoute(unsigned int);
  void ClearRoutes(void);

  //{{AFX_DATA(MyDialog4)
  enum { IDD = IDD_DIALOG4 };
  CListCtrl m_RoutingTable;
  //}}AFX_DATA

  //{{AFX_VIRTUAL(MyDialog4)
protected:
    virtual void DoDataExchange(CDataExchange * pDX);
  //}}AFX_VIRTUAL

protected:

  //{{AFX_MSG(MyDialog4)
    afx_msg void OnOK();
  afx_msg void OnCancel();
  virtual BOOL OnInitDialog();
  //}}AFX_MSG
  DECLARE_MESSAGE_MAP()};

//{{AFX_INSERT_LOCATION}}

#endif

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
