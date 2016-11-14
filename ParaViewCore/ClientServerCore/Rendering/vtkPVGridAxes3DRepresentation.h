/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkPVGridAxes3DRepresentation.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class vtkPVGridAxes3DRepresentation
 * @brief Representation for vtkPVGridAxis3DActor.
 */

#ifndef vtkPVGridAxes3DRepresentation_h
#define vtkPVGridAxes3DRepresentation_h

#include "vtkNew.h"                               // For vtkNew...
#include "vtkPVClientServerCoreRenderingModule.h" // For export macro
#include "vtkPVDataRepresentation.h"

class vtkOutlineFilter;
class vtkPolyData;
class vtkPVCacheKeeper;
class vtkPVGridAxes3DActor;

class VTKPVCLIENTSERVERCORERENDERING_EXPORT vtkPVGridAxes3DRepresentation
  : public vtkPVDataRepresentation
{
public:
  static vtkPVGridAxes3DRepresentation* New();
  vtkTypeMacro(vtkPVGridAxes3DRepresentation, vtkPVDataRepresentation) void PrintSelf(
    ostream& os, vtkIndent indent) VTK_OVERRIDE;

  void SetGridAxes(vtkPVGridAxes3DActor* gridAxes);
  vtkGetObjectMacro(GridAxes, vtkPVGridAxes3DActor)

    void MarkModified() VTK_OVERRIDE;
  void SetVisibility(bool) VTK_OVERRIDE;
  void SetGridAxesVisibility(bool);
  int ProcessViewRequest(vtkInformationRequestKey* request_type, vtkInformation* inInfo,
    vtkInformation* outInfo) VTK_OVERRIDE;

protected:
  vtkPVGridAxes3DRepresentation();
  ~vtkPVGridAxes3DRepresentation();

  int FillInputPortInformation(int port, vtkInformation* info) VTK_OVERRIDE;
  int RequestData(vtkInformation* req, vtkInformationVector** inInfoVec,
    vtkInformationVector* outInfoVec) VTK_OVERRIDE;
  bool AddToView(vtkView* view) VTK_OVERRIDE;
  bool RemoveFromView(vtkView* view) VTK_OVERRIDE;
  bool IsCached(double cache_key) VTK_OVERRIDE;
  void UpdateVisibility();

  bool GridAxesVisibility;

  vtkNew<vtkPolyData> DummyPolyData;
  vtkNew<vtkPVCacheKeeper> CacheKeeper;
  vtkPVGridAxes3DActor* GridAxes;

private:
  vtkPVGridAxes3DRepresentation(const vtkPVGridAxes3DRepresentation&) VTK_DELETE_FUNCTION;
  void operator=(const vtkPVGridAxes3DRepresentation&) VTK_DELETE_FUNCTION;
};

#endif // vtkPVGridAxes3DRepresentation_h
