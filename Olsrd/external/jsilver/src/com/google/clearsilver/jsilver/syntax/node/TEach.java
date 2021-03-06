/* This file was generated by SableCC (http://www.sablecc.org/). */

package com.google.clearsilver.jsilver.syntax.node;

import com.google.clearsilver.jsilver.syntax.analysis.*;

@SuppressWarnings("nls")
public final class TEach extends Token
{
    public TEach()
    {
        super.setText("each");
    }

    public TEach(int line, int pos)
    {
        super.setText("each");
        setLine(line);
        setPos(pos);
    }

    @Override
    public Object clone()
    {
      return new TEach(getLine(), getPos());
    }

    public void apply(Switch sw)
    {
        ((Analysis) sw).caseTEach(this);
    }

    @Override
    public void setText(@SuppressWarnings("unused") String text)
    {
        throw new RuntimeException("Cannot change TEach text.");
    }
}
