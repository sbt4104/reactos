/*
 *  ReactOS W32 Subsystem
 *  Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <w32k.h>

#define NDEBUG
#include <debug.h>

// Some code from the WINE project source (www.winehq.com)


BOOL FASTCALL
IntGdiMoveToEx(DC      *dc,
               int     X,
               int     Y,
               LPPOINT Point)
{
  BOOL  PathIsOpen;

  if ( Point )
  {
    Point->x = dc->w.CursPosX;
    Point->y = dc->w.CursPosY;
  }
  dc->w.CursPosX = X;
  dc->w.CursPosY = Y;

  PathIsOpen = PATH_IsPathOpen(dc->w.path);

  if ( PathIsOpen )
    return PATH_MoveTo ( dc );

  return TRUE;
}

BOOL FASTCALL
IntGdiLineTo(DC  *dc,
             int XEnd,
             int YEnd)
{
  BITMAPOBJ *BitmapObj;
  BOOL      Ret = TRUE;
  PGDIBRUSHOBJ PenBrushObj;
  GDIBRUSHINST PenBrushInst;
  RECTL     Bounds;
  POINT     Points[2];

  if (PATH_IsPathOpen(dc->w.path))
    {
      Ret = PATH_LineTo(dc, XEnd, YEnd);
      if (Ret)
	  {
	    // FIXME - PATH_LineTo should maybe do this...
	    dc->w.CursPosX = XEnd;
	    dc->w.CursPosY = YEnd;
	  }
      return Ret;
    }
  else
    {
      BitmapObj = BITMAPOBJ_LockBitmap ( dc->w.hBitmap );
      if (NULL == BitmapObj)
        {
          SetLastWin32Error(ERROR_INVALID_HANDLE);
          return FALSE;
        }

      Points[0].x = dc->w.CursPosX;
      Points[0].y = dc->w.CursPosY;
      Points[1].x = XEnd;
      Points[1].y = YEnd;

      IntLPtoDP(dc, Points, 2);

      /* FIXME: Is it correct to do this after the transformation? */
      Points[0].x += dc->w.DCOrgX;
      Points[0].y += dc->w.DCOrgY;
      Points[1].x += dc->w.DCOrgX;
      Points[1].y += dc->w.DCOrgY;

      Bounds.left = min(Points[0].x, Points[1].x);
      Bounds.top = min(Points[0].y, Points[1].y);
      Bounds.right = max(Points[0].x, Points[1].x);
      Bounds.bottom = max(Points[0].y, Points[1].y);

      /* get BRUSHOBJ from current pen. */
      PenBrushObj = PENOBJ_LockPen( dc->w.hPen );
      /* FIXME - PenBrushObj can be NULL!!!! Don't assert here! */
      ASSERT(PenBrushObj);

      if (!(PenBrushObj->flAttrs & GDIBRUSH_IS_NULL))
      {
        IntGdiInitBrushInstance(&PenBrushInst, PenBrushObj, dc->XlatePen);
        Ret = IntEngLineTo(&BitmapObj->SurfObj,
                           dc->CombinedClip,
                           &PenBrushInst.BrushObject,
                           Points[0].x, Points[0].y,
                           Points[1].x, Points[1].y,
                           &Bounds,
                           ROP2_TO_MIX(dc->w.ROPmode));
      }

      BITMAPOBJ_UnlockBitmap ( BitmapObj );
      PENOBJ_UnlockPen( PenBrushObj );
    }

  if (Ret)
    {
      dc->w.CursPosX = XEnd;
      dc->w.CursPosY = YEnd;
    }

  return Ret;
}

BOOL FASTCALL
IntGdiPolyBezier(DC      *dc,
                 LPPOINT pt,
                 DWORD   Count)
{
  BOOL ret = FALSE; // default to FAILURE

  if ( PATH_IsPathOpen(dc->w.path) )
  {
    return PATH_PolyBezier ( dc, pt, Count );
  }

  /* We'll convert it into line segments and draw them using Polyline */
  {
    POINT *Pts;
    INT nOut;

    Pts = GDI_Bezier ( pt, Count, &nOut );
    if ( Pts )
    {
      DbgPrint("Pts = %p, no = %d\n", Pts, nOut);
      ret = IntGdiPolyline(dc, Pts, nOut);
      ExFreePool(Pts);
    }
  }

  return ret;
}

BOOL FASTCALL
IntGdiPolyBezierTo(DC      *dc,
                   LPPOINT pt,
                   DWORD  Count)
{
  BOOL ret = FALSE; // default to failure

  if ( PATH_IsPathOpen(dc->w.path) )
    ret = PATH_PolyBezierTo ( dc, pt, Count );
  else /* We'll do it using PolyBezier */
  {
    POINT *npt;
    npt = ExAllocatePoolWithTag(PagedPool, sizeof(POINT) * (Count + 1), TAG_BEZIER);
    if ( npt )
    {
      npt[0].x = dc->w.CursPosX;
      npt[0].y = dc->w.CursPosY;
      memcpy(npt + 1, pt, sizeof(POINT) * Count);
      ret = IntGdiPolyBezier(dc, npt, Count+1);
      ExFreePool(npt);
    }
  }
  if ( ret )
  {
    dc->w.CursPosX = pt[Count-1].x;
    dc->w.CursPosY = pt[Count-1].y;
  }

  return ret;
}

BOOL FASTCALL
IntGdiPolyline(DC      *dc,
               LPPOINT pt,
               int     Count)
{
   BITMAPOBJ *BitmapObj;
   GDIBRUSHOBJ *PenBrushObj;
   GDIBRUSHINST PenBrushInst;
   LPPOINT Points;
   BOOL Ret = TRUE;
   LONG i;

   if (PATH_IsPathOpen(dc->w.path))
      return PATH_Polyline(dc, pt, Count);

   /* Get BRUSHOBJ from current pen. */
   PenBrushObj = PENOBJ_LockPen(dc->w.hPen);
   /* FIXME - PenBrushObj can be NULL! Don't assert here! */
   ASSERT(PenBrushObj);

   if (!(PenBrushObj->flAttrs & GDIBRUSH_IS_NULL))
   {
      Points = EngAllocMem(0, Count * sizeof(POINT), TAG_COORD);
      if (Points != NULL)
      {
         BitmapObj = BITMAPOBJ_LockBitmap(dc->w.hBitmap);
         /* FIXME - BitmapObj can be NULL!!!! Don't assert but handle this case gracefully! */
         ASSERT(BitmapObj);

         RtlCopyMemory(Points, pt, Count * sizeof(POINT));
         IntLPtoDP(dc, Points, Count);

         /* Offset the array of point by the dc->w.DCOrg */
         for (i = 0; i < Count; i++)
         {
            Points[i].x += dc->w.DCOrgX;
            Points[i].y += dc->w.DCOrgY;
         }

         IntGdiInitBrushInstance(&PenBrushInst, PenBrushObj, dc->XlatePen);
         Ret = IntEngPolyline(&BitmapObj->SurfObj, dc->CombinedClip,
                              &PenBrushInst.BrushObject, Points, Count,
                              ROP2_TO_MIX(dc->w.ROPmode));

         BITMAPOBJ_UnlockBitmap(BitmapObj);
         EngFreeMem(Points);
      }
      else
      {
         Ret = FALSE;
      }
   }

   PENOBJ_UnlockPen(PenBrushObj);

   return Ret;
}

BOOL FASTCALL
IntGdiPolylineTo(DC      *dc,
                 LPPOINT pt,
                 DWORD   Count)
{
  BOOL ret = FALSE; // default to failure

  if(PATH_IsPathOpen(dc->w.path))
  {
    ret = PATH_PolylineTo(dc, pt, Count);
  }
  else /* do it using Polyline */
  {
    POINT *pts = ExAllocatePoolWithTag(PagedPool, sizeof(POINT) * (Count + 1), TAG_SHAPE);
    if ( pts )
    {
      pts[0].x = dc->w.CursPosX;
      pts[0].y = dc->w.CursPosY;
      memcpy( pts + 1, pt, sizeof(POINT) * Count);
      ret = IntGdiPolyline(dc, pts, Count + 1);
      ExFreePool(pts);
    }
  }
  if ( ret )
  {
    dc->w.CursPosX = pt[Count-1].x;
    dc->w.CursPosY = pt[Count-1].y;
  }

  return ret;
}

INT FASTCALL
IntGdiGetArcDirection(DC *dc)
{
  return dc->w.ArcDirection;
}

BOOL FASTCALL
IntGdiArc(DC  *dc,
          int LeftRect,
          int TopRect,
          int RightRect,
          int BottomRect,
          int XStartArc,
          int YStartArc,
          int XEndArc,
          int YEndArc)
{
  if(PATH_IsPathOpen(dc->w.path))
  {
    return PATH_Arc(dc, LeftRect, TopRect, RightRect, BottomRect,
                    XStartArc, YStartArc, XEndArc, YEndArc);
  }

  // FIXME
//   EngArc(dc, LeftRect, TopRect, RightRect, BottomRect, UNIMPLEMENTED
//          XStartArc, YStartArc, XEndArc, YEndArc);

  return TRUE;
}

BOOL FASTCALL
IntGdiPolyPolyline(DC      *dc,
                   LPPOINT pt,
                   LPDWORD PolyPoints,
                   DWORD   Count)
{
  int i;
  LPPOINT pts;
  LPDWORD pc;
  BOOL ret = FALSE; // default to failure
  pts = pt;
  pc = PolyPoints;

  for (i = 0; i < Count; i++)
  {
    ret = IntGdiPolyline ( dc, pts, *pc );
    if (ret == FALSE)
    {
      return ret;
    }
    pts+=*pc++;
  }

  return ret;
}

/******************************************************************************/

BOOL
APIENTRY
NtGdiAngleArc(
    IN HDC hdc,
    IN INT x,
    IN INT y,
    IN DWORD dwRadius,
    IN DWORD dwStartAngle,
    IN DWORD dwSweepAngle)
{
  UNIMPLEMENTED;
  return FALSE;
}

BOOL
STDCALL
NtGdiArc(HDC  hDC,
        int  LeftRect,
        int  TopRect,
        int  RightRect,
        int  BottomRect,
        int  XStartArc,
        int  YStartArc,
        int  XEndArc,
        int  YEndArc)
{
  DC *dc;
  BOOL Ret;

  dc = DC_LockDc (hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  Ret = IntGdiArc(dc,
                  LeftRect,
                  TopRect,
                  RightRect,
                  BottomRect,
                  XStartArc,
                  YStartArc,
                  XEndArc,
                  YEndArc);

  DC_UnlockDc( dc );
  return Ret;
}

BOOL
STDCALL
NtGdiArcTo(HDC  hDC,
          int  LeftRect,
          int  TopRect,
          int  RightRect,
          int  BottomRect,
          int  XRadial1,
          int  YRadial1,
          int  XRadial2,
          int  YRadial2)
{
  BOOL result;
  DC *dc;

  dc = DC_LockDc (hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  // Line from current position to starting point of arc
  if ( !IntGdiLineTo(dc, XRadial1, YRadial1) )
  {
    DC_UnlockDc(dc);
    return FALSE;
  }

  //dc = DC_LockDc(hDC);

  //if(!dc) return FALSE;

  // Then the arc is drawn.
  result = IntGdiArc(dc, LeftRect, TopRect, RightRect, BottomRect,
                     XRadial1, YRadial1, XRadial2, YRadial2);

  //DC_UnlockDc(dc);

  // If no error occured, the current position is moved to the ending point of the arc.
  if(result)
    IntGdiMoveToEx(dc, XRadial2, YRadial2, NULL);

  DC_UnlockDc(dc);

  return result;
}

INT
STDCALL
NtGdiGetArcDirection(HDC  hDC)
{
  PDC dc = DC_LockDc (hDC);
  int ret = 0; // default to failure

  if ( dc )
  {
    ret = IntGdiGetArcDirection ( dc );
    DC_UnlockDc(dc);
  }
  else
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
  }

  return ret;
}

BOOL
STDCALL
NtGdiLineTo(HDC  hDC,
           int  XEnd,
           int  YEnd)
{
  DC *dc;
  BOOL Ret;

  dc = DC_LockDc(hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  Ret = IntGdiLineTo(dc, XEnd, YEnd);

  DC_UnlockDc(dc);
  return Ret;
}

BOOL
STDCALL
NtGdiMoveToEx(HDC      hDC,
             int      X,
             int      Y,
             LPPOINT  Point)
{
  DC *dc;
  POINT SafePoint;
  NTSTATUS Status = STATUS_SUCCESS;
  BOOL Ret;

  dc = DC_LockDc(hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  if(Point)
  {
    _SEH_TRY
    {
      ProbeForRead(Point,
                   sizeof(POINT),
                   1);
      SafePoint = *Point;
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if(!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
  }

  Ret = IntGdiMoveToEx(dc, X, Y, (Point ? &SafePoint : NULL));

  DC_UnlockDc(dc);
  return Ret;
}

BOOL
STDCALL
NtGdiPolyBezier(HDC           hDC,
               CONST LPPOINT  pt,
               DWORD          Count)
{
  DC *dc;
  LPPOINT Safept;
  NTSTATUS Status = STATUS_SUCCESS;
  BOOL Ret;

  dc = DC_LockDc(hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  if(Count > 0)
  {
    _SEH_TRY
    {
      ProbeForRead(pt,
                   Count * sizeof(POINT),
                   1);
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;
    
    if (!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
    
    Safept = ExAllocatePoolWithTag(PagedPool, sizeof(POINT) * Count, TAG_BEZIER);
    if(!Safept)
    {
      DC_UnlockDc(dc);
      SetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }

    _SEH_TRY
    {
      /* pointers were already probed */
      RtlCopyMemory(Safept,
                    pt,
                    Count * sizeof(POINT));
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if(!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
  }
  else
  {
    DC_UnlockDc(dc);
    SetLastWin32Error(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  Ret = IntGdiPolyBezier(dc, Safept, Count);

  ExFreePool(Safept);
  DC_UnlockDc(dc);

  return Ret;
}

BOOL
STDCALL
NtGdiPolyBezierTo(HDC  hDC,
                 CONST LPPOINT  pt,
                 DWORD  Count)
{
  DC *dc;
  LPPOINT Safept;
  NTSTATUS Status = STATUS_SUCCESS;
  BOOL Ret;

  dc = DC_LockDc(hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  if(Count > 0)
  {
    _SEH_TRY
    {
      ProbeForRead(pt,
                   Count * sizeof(POINT),
                   1);
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if (!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
    
    Safept = ExAllocatePoolWithTag(PagedPool, sizeof(POINT) * Count, TAG_BEZIER);
    if(!Safept)
    {
      DC_UnlockDc(dc);
      SetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }

    _SEH_TRY
    {
      /* pointers were already probed */
      RtlCopyMemory(Safept,
                    pt,
                    Count * sizeof(POINT));
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if(!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
  }
  else
  {
    DC_UnlockDc(dc);
    SetLastWin32Error(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  Ret = IntGdiPolyBezierTo(dc, Safept, Count);

  ExFreePool(Safept);
  DC_UnlockDc(dc);

  return Ret;
}

BOOL
APIENTRY
NtGdiPolyDraw(
    IN HDC hdc,
    IN LPPOINT ppt,
    IN LPBYTE pjAttr,
    IN ULONG cpt)
{
  UNIMPLEMENTED;
  return FALSE;
}

BOOL
STDCALL
NtGdiPolyline(HDC            hDC,
             CONST LPPOINT  pt,
             int            Count)
{
  DC *dc;
  LPPOINT Safept;
  NTSTATUS Status = STATUS_SUCCESS;
  BOOL Ret;

  dc = DC_LockDc(hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  if(Count >= 2)
  {
    _SEH_TRY
    {
      ProbeForRead(pt,
                   Count * sizeof(POINT),
                   1);
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if (!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
    
    Safept = ExAllocatePoolWithTag(PagedPool, sizeof(POINT) * Count, TAG_SHAPE);
    if(!Safept)
    {
      DC_UnlockDc(dc);
      SetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }

    _SEH_TRY
    {
      /* pointers were already probed */
      RtlCopyMemory(Safept,
                    pt,
                    Count * sizeof(POINT));
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if(!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
  }
  else
  {
    DC_UnlockDc(dc);
    SetLastWin32Error(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  Ret = IntGdiPolyline(dc, Safept, Count);

  ExFreePool(Safept);
  DC_UnlockDc(dc);

  return Ret;
}

BOOL
STDCALL
NtGdiPolylineTo(HDC            hDC,
               CONST LPPOINT  pt,
               DWORD          Count)
{
  DC *dc;
  LPPOINT Safept;
  NTSTATUS Status = STATUS_SUCCESS;
  BOOL Ret;

  dc = DC_LockDc(hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  if(Count > 0)
  {
    _SEH_TRY
    {
      ProbeForRead(pt,
                   Count * sizeof(POINT),
                   1);
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if (!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
    
    Safept = ExAllocatePoolWithTag(PagedPool, sizeof(POINT) * Count, TAG_SHAPE);
    if(!Safept)
    {
      DC_UnlockDc(dc);
      SetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }

    _SEH_TRY
    {
      /* pointers were already probed */
      RtlCopyMemory(Safept,
                    pt,
                    Count * sizeof(POINT));
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if(!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
  }
  else
  {
    DC_UnlockDc(dc);
    SetLastWin32Error(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  Ret = IntGdiPolylineTo(dc, Safept, Count);

  ExFreePool(Safept);
  DC_UnlockDc(dc);

  return Ret;
}

BOOL
STDCALL
NtGdiPolyPolyline(HDC            hDC,
                 CONST LPPOINT  pt,
                 CONST LPDWORD  PolyPoints,
                 DWORD          Count)
{
  DC *dc;
  LPPOINT Safept;
  LPDWORD SafePolyPoints;
  NTSTATUS Status = STATUS_SUCCESS;
  BOOL Ret;

  dc = DC_LockDc(hDC);
  if(!dc)
  {
    SetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (dc->IsIC)
  {
    DC_UnlockDc(dc);
    /* Yes, Windows really returns TRUE in this case */
    return TRUE;
  }

  if(Count > 0)
  {
    _SEH_TRY
    {
      ProbeForRead(pt,
                   Count * sizeof(POINT),
                   1);
      ProbeForRead(PolyPoints,
                   Count * sizeof(DWORD),
                   1);
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if (!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      SetLastNtError(Status);
      return FALSE;
    }
    
    Safept = ExAllocatePoolWithTag(PagedPool, (sizeof(POINT) + sizeof(DWORD)) * Count, TAG_SHAPE);
    if(!Safept)
    {
      DC_UnlockDc(dc);
      SetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }

    SafePolyPoints = (LPDWORD)&Safept[Count];
    
    _SEH_TRY
    {
      /* pointers were already probed */
      RtlCopyMemory(Safept,
                    pt,
                    Count * sizeof(POINT));
      RtlCopyMemory(SafePolyPoints,
                    PolyPoints,
                    Count * sizeof(DWORD));
    }
    _SEH_HANDLE
    {
      Status = _SEH_GetExceptionCode();
    }
    _SEH_END;

    if(!NT_SUCCESS(Status))
    {
      DC_UnlockDc(dc);
      ExFreePool(Safept);
      SetLastNtError(Status);
      return FALSE;
    }
  }
  else
  {
    DC_UnlockDc(dc);
    SetLastWin32Error(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  Ret = IntGdiPolyPolyline(dc, Safept, SafePolyPoints, Count);

  ExFreePool(Safept);
  DC_UnlockDc(dc);

  return Ret;
}

int
STDCALL
NtGdiSetArcDirection(HDC  hDC,
                    int  ArcDirection)
{
  PDC  dc;
  INT  nOldDirection = 0; // default to FAILURE

  dc = DC_LockDc (hDC);
  if ( !dc ) return 0;

  if ( ArcDirection == AD_COUNTERCLOCKWISE || ArcDirection == AD_CLOCKWISE )
  {
    nOldDirection = dc->w.ArcDirection;
    dc->w.ArcDirection = ArcDirection;
  }

  DC_UnlockDc( dc );
  return nOldDirection;
}
/* EOF */
