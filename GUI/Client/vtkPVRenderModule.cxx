/*=========================================================================

  Program:   ParaView
  Module:    vtkPVRenderModule.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkPVRenderModule.h"

#include "vtkCamera.h"
#include "vtkCollectionIterator.h"
#include "vtkCallbackCommand.h"
#include "vtkCommand.h"
#include "vtkMultiProcessController.h"
#include "vtkObjectFactory.h"
#include "vtkPVApplication.h"
#include "vtkPVConfig.h"
#include "vtkPVData.h"
#include "vtkPVDataInformation.h"
#include "vtkPVGenericRenderWindowInteractor.h"
#include "vtkPVPart.h"
#include "vtkPVPartDisplay.h"
#include "vtkPVProcessModule.h"
#include "vtkPVSourceCollection.h"
#include "vtkPVSourceList.h"
#include "vtkPVWindow.h"
#include "vtkPVSource.h"
#include "vtkPolyData.h"
#include "vtkPolyDataMapper.h"
#include "vtkRenderer.h"
#include "vtkRenderWindow.h"
#include "vtkFloatArray.h"
#include "vtkString.h"
#include "vtkTimerLog.h"
#include "vtkToolkits.h"

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPVRenderModule);
vtkCxxRevisionMacro(vtkPVRenderModule, "1.28");

//===========================================================================
//***************************************************************************
class vtkPVRenderModuleObserver : public vtkCommand
{
public:
  static vtkPVRenderModuleObserver *New() 
    {return new vtkPVRenderModuleObserver;};

  vtkPVRenderModuleObserver()
    {
      this->PVRenderModule = 0;
    }

  virtual void Execute(vtkObject* wdg, unsigned long event,  
                       void*)
    {
      if ( this->PVRenderModule )
        {
        if (event == vtkCommand::StartEvent &&
            vtkRenderer::SafeDownCast(wdg))
          {
          this->PVRenderModule->StartRenderEvent();
          }
        }
    }

  vtkPVRenderModule* PVRenderModule;
};
//***************************************************************************
//===========================================================================

//----------------------------------------------------------------------------
vtkPVRenderModule::vtkPVRenderModule()
{
  this->PVApplication = NULL;
  this->TotalVisibleMemorySizeValid = 0;
  
  this->Displays = vtkCollection::New();

  this->Renderer = 0;
  this->Renderer2D = 0;
  this->RenderWindow = 0;
  this->RendererID.ID     = 0;
  this->Renderer2DID.ID   = 0;
  this->RenderWindowID.ID = 0;
  this->InteractiveRenderTime    = 0;
  this->StillRenderTime          = 0;

  this->ResetCameraClippingRangeTag = 0;

  this->RenderInterruptsEnabled = 1;

  this->Observer = vtkPVRenderModuleObserver::New();
  this->Observer->PVRenderModule = this;
}

//----------------------------------------------------------------------------
vtkPVRenderModule::~vtkPVRenderModule()
{
  vtkPVApplication *pvApp = this->PVApplication;
  vtkPVProcessModule* pm = 0;
  if(pvApp)
    {
    pm = pvApp->GetProcessModule();
    }
  
  if (this->Renderer && this->ResetCameraClippingRangeTag > 0)
    {
    this->Renderer->RemoveObserver(this->ResetCameraClippingRangeTag);
    this->ResetCameraClippingRangeTag = 0;
    }

  this->Displays->Delete();
  this->Displays = NULL;

  
  if (this->Renderer)
    {
    if (this->ResetCameraClippingRangeTag > 0)
      {
      this->Renderer->RemoveObserver(this->ResetCameraClippingRangeTag);
      this->ResetCameraClippingRangeTag = 0;
      }

    if ( pm )
      {
      pm->DeleteStreamObject(this->RendererID);
      }
    this->RendererID.ID = 0;
    }
  
  if (this->Renderer2D)
    {
    if ( pm )
      {
      pm->DeleteStreamObject(this->Renderer2DID);
      }
    this->Renderer2DID.ID = 0;
    }

  if (this->RenderWindow)
    {
    if ( pm )
      { 
      pm->DeleteStreamObject(this->RenderWindowID);
      }
    this->RenderWindowID.ID = 0;
    }
  if(pm)
    {
    pm->SendStreamToClientAndRenderServer();
    }
  
  this->Observer->Delete();
  this->Observer = NULL;

  this->SetPVApplication(NULL);
}

//----------------------------------------------------------------------------
// This is a bit of a pain.  I do ResetCameraClippingRange as a call back
// because the PVInteractorStyles call ResetCameraClippingRange 
// directly on the renderer.  Since they are PV styles, I might
// have them call the render module directly like they do for render.
void vtkPVRenderModuleResetCameraClippingRange(
  vtkObject *caller, unsigned long vtkNotUsed(event),void *clientData, void *)
{
  double bds[6];
  double range1[2];
  double range2[2];

  vtkPVRenderModule *self = (vtkPVRenderModule *)clientData;
  vtkRenderer *ren = (vtkRenderer*)caller;

  // Get default clipping range.
  // Includes 3D widgets but not all processes.
  ren->GetActiveCamera()->GetClippingRange(range1);

  self->ComputeVisiblePropBounds(bds);
  ren->ResetCameraClippingRange(bds);
  // Get part clipping range.
  // Includes all process partitions, but not 3d Widgets.
  ren->GetActiveCamera()->GetClippingRange(range2);

  // Merge
  if (range1[0] < range2[0])
    {
    range2[0] = range1[0];
    }
  if (range1[1] > range2[1])
    {
    range2[1] = range1[1];
    }
  // Includes all process partitions and 3D Widgets.
  ren->GetActiveCamera()->SetClippingRange(range2);
}


//----------------------------------------------------------------------------
void vtkPVRenderModule::StartRenderEvent()
{
  // make the overlay renderer match the view of the 3d renderer
  if (this->Renderer2D)
    {
    this->Renderer2D->GetActiveCamera()->SetClippingRange(
      this->Renderer->GetActiveCamera()->GetClippingRange());
    this->Renderer2D->GetActiveCamera()->SetPosition(
      this->Renderer->GetActiveCamera()->GetPosition());
    this->Renderer2D->GetActiveCamera()->SetFocalPoint(
      this->Renderer->GetActiveCamera()->GetFocalPoint());
    this->Renderer2D->GetActiveCamera()->SetViewUp(
      this->Renderer->GetActiveCamera()->GetViewUp());

    this->Renderer2D->GetActiveCamera()->SetParallelProjection(
      this->Renderer->GetActiveCamera()->GetParallelProjection());
    this->Renderer2D->GetActiveCamera()->SetParallelScale(
      this->Renderer->GetActiveCamera()->GetParallelScale());
    }
}


//----------------------------------------------------------------------------
void vtkPVRenderModule::SetPVApplication(vtkPVApplication *pvApp)
{
  if (this->PVApplication)
    {
    if (pvApp == NULL)
      {
      this->PVApplication->UnRegister(this);
      this->PVApplication = NULL;
      return;
      }
    vtkErrorMacro("PVApplication already set.");
    return;
    }
  if (pvApp == NULL)
    {
    return;
    }  
  vtkPVProcessModule *pm = pvApp->GetProcessModule();
  vtkClientServerStream& stream = pm->GetStream();
  // Maybe I should not reference count this object to avoid
  // a circular reference.
  this->PVApplication = pvApp;
  this->PVApplication->Register(this);

  this->RendererID = pm->NewStreamObject("vtkRenderer");
  this->Renderer2DID = pm->NewStreamObject("vtkRenderer");
  this->RenderWindowID = pm->NewStreamObject("vtkRenderWindow");
  pm->SendStreamToClientAndRenderServer();
  this->Renderer = 
    vtkRenderer::SafeDownCast(
      pm->GetObjectFromID(this->RendererID));
  this->Renderer2D = 
    vtkRenderer::SafeDownCast(
      pm->GetObjectFromID(this->Renderer2DID));
  this->RenderWindow = 
    vtkRenderWindow::SafeDownCast(
      pm->GetObjectFromID(this->RenderWindowID));


  if (pvApp->GetUseStereoRendering())
    {
    this->RenderWindow->StereoCapableWindowOn();
    this->RenderWindow->StereoRenderOn();
    }

  // We cannot erase the zbuffer.  We need it for picking.  
  stream << vtkClientServerStream::Invoke
         << this->Renderer2DID << "EraseOff" 
         << vtkClientServerStream::End;
  stream << vtkClientServerStream::Invoke
         << this->Renderer2DID << "SetLayer" << 2 
         << vtkClientServerStream::End;
  stream << vtkClientServerStream::Invoke
         << this->RenderWindowID << "SetNumberOfLayers" << 3
         << vtkClientServerStream::End;
  stream << vtkClientServerStream::Invoke
         << this->RenderWindowID << "AddRenderer" << this->RendererID
         << vtkClientServerStream::End;
  stream << vtkClientServerStream::Invoke
         << this->RenderWindowID << "AddRenderer" << this->Renderer2DID
         << vtkClientServerStream::End;
  pm->SendStreamToClientAndRenderServer();
    
  // the 2d renderer must be kept in sync with the main renderer
  this->Renderer->AddObserver(
    vtkCommand::StartEvent, this->Observer);  

  // Make sure we have a chance to set the clipping range properly.
  vtkCallbackCommand* cbc;
  cbc = vtkCallbackCommand::New();
  cbc->SetCallback(vtkPVRenderModuleResetCameraClippingRange);
  cbc->SetClientData((void*)this);
  // ren will delete the cbc when the observer is removed.
  this->ResetCameraClippingRangeTag = 
    this->Renderer->AddObserver(vtkCommand::ResetCameraClippingRangeEvent,cbc);
  cbc->Delete();
}

//----------------------------------------------------------------------------
vtkRenderWindow *vtkPVRenderModule::GetRenderWindow()
{
  return this->RenderWindow;
}

//----------------------------------------------------------------------------
vtkRenderer *vtkPVRenderModule::GetRenderer()
{
  return this->Renderer;
}

//----------------------------------------------------------------------------
vtkRenderer *vtkPVRenderModule::GetRenderer2D()
{
  return this->Renderer2D;
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::SetBackgroundColor(float r, float g, float b)
{
  vtkPVApplication *pvApp = this->GetPVApplication();
  vtkPVProcessModule *pm = pvApp->GetProcessModule();
  vtkClientServerStream& stream = pm->GetStream();
  stream << vtkClientServerStream::Invoke << this->RendererID << "SetBackground"
         << r << g << b << vtkClientServerStream::End;
  pm->SendStreamToClientAndRenderServer();
}

//-----------------------------------------------------------------------------
void vtkPVRenderModule::CacheUpdate(int idx, int total)
{
  vtkObject* object;
  vtkPVPartDisplay* pDisp;
  this->Displays->InitTraversal();
  while ( (object = this->Displays->GetNextItemAsObject()) )
    {
    pDisp = vtkPVPartDisplay::SafeDownCast(object);
    if (pDisp->GetVisibility())
      {
      pDisp->CacheUpdate(idx, total);
      }
    }
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::InvalidateAllGeometries()
{
  vtkObject* object;
  vtkPVPartDisplay* pDisp;
  this->Displays->InitTraversal();
  while ( (object = this->Displays->GetNextItemAsObject()) )
    {
    pDisp = vtkPVPartDisplay::SafeDownCast(object);
    pDisp->InvalidateGeometry();
    }
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::ComputeVisiblePropBounds(double bds[6])
{
  double* tmp;
  vtkObject* object;
  vtkPVPartDisplay* pDisp;
  vtkPVPart* part;

  // Compute the bounds for our sources.
  bds[0] = bds[2] = bds[4] = VTK_DOUBLE_MAX;
  bds[1] = bds[3] = bds[5] = -VTK_DOUBLE_MAX;
  this->Displays->InitTraversal();
  while ( (object = this->Displays->GetNextItemAsObject()) )
    {
    pDisp = vtkPVPartDisplay::SafeDownCast(object);
    if (pDisp && pDisp->GetVisibility())
      {
      part = pDisp->GetPart();
      tmp = part->GetDataInformation()->GetBounds();
      if (tmp[0] < bds[0]) { bds[0] = tmp[0]; }  
      if (tmp[1] > bds[1]) { bds[1] = tmp[1]; }  
      if (tmp[2] < bds[2]) { bds[2] = tmp[2]; }  
      if (tmp[3] > bds[3]) { bds[3] = tmp[3]; }  
      if (tmp[4] < bds[4]) { bds[4] = tmp[4]; }  
      if (tmp[5] > bds[5]) { bds[5] = tmp[5]; }  
      }
    }

  if ( bds[0] > bds[1])
    {
    bds[0] = bds[2] = bds[4] = -1.0;
    bds[1] = bds[3] = bds[5] = 1.0;
    }
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::AddDisplay(vtkPVDisplay* disp)
{
  this->Displays->AddItem(disp);
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::RemoveDisplay(vtkPVDisplay* disp)
{
  this->Displays->RemoveItem(disp);
}

//----------------------------------------------------------------------------
vtkPVPartDisplay* vtkPVRenderModule::CreatePartDisplay()
{
  return vtkPVPartDisplay::New();
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::AddPVSource(vtkPVSource *pvs)
{
  vtkPVApplication *pvApp = this->GetPVApplication();
  vtkPVPart *part;
  vtkPVPartDisplay *pDisp;
  int num, idx;
  
  if (pvs == NULL)
    {
    return;
    }  
  
  num = pvs->GetNumberOfParts();
  for (idx = 0; idx < num; ++idx)
    {
    part = pvs->GetPart(idx);
    // Create a part display for each part.
    pDisp = this->CreatePartDisplay();
    this->Displays->AddItem(pDisp);
    pDisp->SetPVApplication(pvApp);
    part->SetPartDisplay(pDisp);
    pDisp->SetPart(part);

    if (part && pDisp->GetPropID().ID != 0)
      { 
      vtkPVProcessModule *pm = pvApp->GetProcessModule();
      vtkClientServerStream& stream = pm->GetStream();
      stream << vtkClientServerStream::Invoke << this->RendererID << "AddProp"
             << pDisp->GetPropID() << vtkClientServerStream::End;
      stream << vtkClientServerStream::Invoke << this->RendererID << "AddProp"
             << pDisp->GetVolumeID() << vtkClientServerStream::End;
      pm->SendStreamToClientAndRenderServer();
      }
    pDisp->Delete();
    }
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::RemovePVSource(vtkPVSource *pvs)
{
  int idx, num;
  vtkPVApplication *pvApp = this->GetPVApplication();
  vtkPVPart *part;
  vtkPVPartDisplay *pDisp;

  if (pvs == NULL)
    {
    return;
    }

  num = pvs->GetNumberOfParts();
  for (idx = 0; idx < num; ++idx)
    {
    part = pvs->GetPart(idx);
    pDisp = part->GetPartDisplay();
    if (pDisp)
      {
      this->Displays->RemoveItem(pDisp);
      if (pDisp->GetPropID().ID != 0)
        {  
        vtkPVProcessModule *pm = pvApp->GetProcessModule();
        vtkClientServerStream& stream = pm->GetStream();
        stream << vtkClientServerStream::Invoke << this->RendererID << "RemoveProp"
               << pDisp->GetPropID() << vtkClientServerStream::End;
        pm->SendStreamToClientAndRenderServer();
        }
      part->SetPartDisplay(NULL);
      }
    }
}


//----------------------------------------------------------------------------
void vtkPVRenderModule::UpdateAllDisplays()
{
  vtkObject* object;
  vtkPVPartDisplay* pDisp;
  vtkPVApplication* pvApp = this->GetPVApplication();
  pvApp->SendPrepareProgress();

  this->Displays->InitTraversal();
  while ( (object = this->Displays->GetNextItemAsObject()) )
    {
    pDisp = vtkPVPartDisplay::SafeDownCast(object);
    if (pDisp && pDisp->GetVisibility())
      {
      pDisp->Update();
      }
    }
  pvApp->SendCleanupPendingProgress();
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::InteractiveRender()
{
  this->UpdateAllDisplays();

  vtkTimerLog::MarkStartEvent("Interactive Render");
  this->RenderWindow->Render();
  vtkTimerLog::MarkEndEvent("Interactive Render");
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::StillRender()
{
  this->UpdateAllDisplays();

  // Still Render can get called some funky ways.
  // Interactive renders get called through the PVInteractorStyles
  // which cal ResetCameraClippingRange on the Renderer.
  // We could convert them to call a method on the module directly ...
  this->Renderer->ResetCameraClippingRange();

  this->GetPVApplication()->SetGlobalLODFlag(0);
  vtkTimerLog::MarkStartEvent("Still Render");
  this->RenderWindow->SetDesiredUpdateRate(0.002);
  this->RenderWindow->Render();
  vtkTimerLog::MarkEndEvent("Still Render");
}



//----------------------------------------------------------------------------
void vtkPVRenderModule::SetUseTriangleStrips(int val)
{
  vtkObject* object;
  vtkPVPartDisplay* pDisp;
  vtkPVApplication *pvApp;

  pvApp = this->GetPVApplication();

  this->Displays->InitTraversal();
  while ( (object = this->Displays->GetNextItemAsObject()) )
    {
    pDisp = vtkPVPartDisplay::SafeDownCast(object);
    if (pDisp && pDisp->GetPart())
      {
      vtkPVProcessModule* pm = pvApp->GetProcessModule();
      vtkClientServerStream& stream = pm->GetStream();
      stream << vtkClientServerStream::Invoke << pDisp->GetGeometryID()
             <<  "SetUseStrips" << val << vtkClientServerStream::End;
      pm->SendStreamToClientAndRenderServer();
      }
    }

  if (val)
    {
    vtkTimerLog::MarkEvent("--- Enable triangle strips.");
    }
  else
    {
    vtkTimerLog::MarkEvent("--- Disable triangle strips.");
    }

}


//----------------------------------------------------------------------------
void vtkPVRenderModule::SetUseParallelProjection(int val)
{
  if (val)
    {
    vtkTimerLog::MarkEvent("--- Enable parallel projection.");
    this->Renderer->GetActiveCamera()->ParallelProjectionOn();
    }
  else
    {
    vtkTimerLog::MarkEvent("--- Disable parallel projection.");
    this->Renderer->GetActiveCamera()->ParallelProjectionOff();
    }
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::SetUseImmediateMode(int val)
{
  vtkObject* object;
  vtkPVPartDisplay* pDisp;

  this->Displays->InitTraversal();
  while ( (object = this->Displays->GetNextItemAsObject()) )
    {
    pDisp = vtkPVPartDisplay::SafeDownCast(object);
    if (pDisp)
      {
      pDisp->SetUseImmediateMode(val);
      }
    }

  if (val)
    {
    vtkTimerLog::MarkEvent("--- Disable display lists.");
    }
  else
    {
    vtkTimerLog::MarkEvent("--- Enable display lists.");
    }
}

//----------------------------------------------------------------------------
float vtkPVRenderModule::GetZBufferValue(int x, int y)
{
  vtkFloatArray *array = vtkFloatArray::New();
  float val;

  array->SetNumberOfTuples(1);
  this->RenderWindow->GetZbufferData(x,y, x, y,
                                     array);
  val = array->GetValue(0);
  array->Delete();
  return val;  
}

//----------------------------------------------------------------------------
void vtkPVRenderModule::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
  os << indent << "InteractiveRenderTime: " 
     << this->InteractiveRenderTime << endl;
//   os << indent << "RenderWindowTclName: " 
//      << (this->GetRenderWindowTclName()?this->GetRenderWindowTclName():"<none>") << endl;
//   os << indent << "RendererTclName: " 
//      << (this->GetRendererTclName()?this->GetRendererTclName():"<none>") << endl;
  os << indent << "StillRenderTime: " << this->StillRenderTime << endl;
  os << indent << "RenderInterruptsEnabled: " 
     << (this->RenderInterruptsEnabled ? "on" : "off") << endl;

  if (this->PVApplication)
    {
    os << indent << "PVApplication: " << this->PVApplication << endl;
    }
  else
    {
    os << indent << "PVApplication: NULL" << endl;
    }


  os << indent << "TotalVisibleMemorySizeValid: " 
     << this->TotalVisibleMemorySizeValid << endl;
}

