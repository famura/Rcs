/*******************************************************************************

  Copyright (c) 2017, Honda Research Institute Europe GmbH.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  3. All advertising materials mentioning features or use of this software
     must display the following acknowledgement: This product includes
     software developed by the Honda Research Institute Europe GmbH.

  4. Neither the name of the copyright holder nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

#include "Rcs_BVHParser.h"
#include "Rcs_macros.h"
#include "Rcs_typedef.h"
#include "Rcs_utils.h"
#include "Rcs_body.h"
#include "Rcs_joint.h"
#include "Rcs_Vec3d.h"
#include "Rcs_Mat3d.h"
#include "Rcs_basicMath.h"

#include <stdio.h>


#define STRNCASEEQ(a,b,n) (strncasecmp((a),(b),(n))==0)

/*******************************************************************************
 *
 ******************************************************************************/
static bool findKeyword(const char* keyword, FILE* fd)
{
  const unsigned int bufLen = 32;
  int nItemsRead = 0;
  char buf[bufLen];

  do
    {
      nItemsRead = fscanf(fd, "%31s", buf);
      RLOG(10, "Reading keyword \"%s\"", buf);

      if (nItemsRead != 1)
        {
          RLOG(1, "Keyword \"%s\": Couldn't read 1 item: %d",
               keyword, nItemsRead);
          continue;
        }

    } while ((nItemsRead!=EOF) && (!STRNCASEEQ(buf, keyword, bufLen)));

  return (nItemsRead!=EOF) ? true : false;
}

/*******************************************************************************
 *
 ******************************************************************************/
static RcsShape* createFrameShape(double scale)
{
  RcsShape* shape = RALLOC(RcsShape);
  HTr_setIdentity(&shape->A_CB);
  shape->scale = scale;
  shape->type = RCSSHAPE_REFFRAME;
  shape->computeType |= RCSSHAPE_COMPUTE_GRAPHICS;
  shape->extents[0] = 0.9;
  shape->extents[1] = 0.9;
  shape->extents[2] = 0.9;

  return shape;
}

/*******************************************************************************
 *
 ******************************************************************************/
static bool parseRecursive(char* buf, RcsGraph* self, RcsBody* body, FILE* fd,
                           const double offset[3], double linearScaleToSI,
                           bool Z_up_x_forward)
{
  if (STRCASEEQ(buf, "ROOT"))
  {
    RcsBody* child = RALLOC(RcsBody);
    child->A_BI = HTr_create();
    fscanf(fd, "%63s", buf);   // Body name
    child->name = String_clone(buf);
    child->Inertia = HTr_create();
    Mat3d_setZero(child->Inertia->rot);
    if (Z_up_x_forward)
      {
        child->A_BP = HTr_create();
        //Mat3d_fromEulerAngles2(child->A_BP->rot, M_PI_2, M_PI_2, 0.0);
      }
    RcsBody_addShape(child, createFrameShape(0.5));
    RcsGraph_insertBody(self, body, child);
    fscanf(fd, "%63s", buf);   // Curly brace open
    fscanf(fd, "%63s", buf);   // Next keyword

    RLOG(5, "Recursing after ROOT with next keyword %s", buf);
    parseRecursive(buf, self, child, fd, Vec3d_zeroVec(), linearScaleToSI,
                   Z_up_x_forward);
  }
  else if (STRCASEEQ(buf, "OFFSET"))
  {
    double offs[3];
    fscanf(fd, "%lf %lf %lf", &offs[0], &offs[1], &offs[2]);
    Vec3d_constMulSelf(offs, linearScaleToSI);
    fscanf(fd, "%63s", buf);   // Next keyword
    RLOG(5, "Recursing after OFFSET with next keyword %s", buf);
    parseRecursive(buf, self, body, fd, offs, linearScaleToSI,
                   Z_up_x_forward);
  }
  else if (STRCASEEQ(buf, "CHANNELS"))
  {
    int nChannels = 0;
    fscanf(fd, "%d", &nChannels);
    RLOG(5, "Found %d channels", nChannels);

    for (int i=0; i<nChannels; ++i)
    {
      fscanf(fd, "%63s", buf);   // direction

      RcsJoint* jnt = RALLOC(RcsJoint);
      char a[128];
      sprintf(a, "%s_jnt_%s", body->name, buf);
      jnt->name = String_clone(a);
      jnt->weightJL = 1.0;
      jnt->weightMetric = 1.0;
      jnt->ctrlType = RCSJOINT_CTRL_POSITION;

      if ((i==0) && (Vec3d_sqrLength(offset)>0.0))
      {
        jnt->A_JP = HTr_create();
        Vec3d_copy(jnt->A_JP->org, offset);
      }

      if (STRNCASEEQ(buf, "Xposition", 63))
      {
        jnt->type = RCSJOINT_TRANS_X;
        jnt->q_min = -1.0;
        jnt->q_max = 1.0;
        jnt->dirIdx = 0;
      }
      else if (STRNCASEEQ(buf, "Yposition", 63))
      {
        jnt->type = RCSJOINT_TRANS_Y;
        jnt->q_min = -1.0;
        jnt->q_max = 1.0;
        jnt->dirIdx = 1;
      }
      else if (STRNCASEEQ(buf, "Zposition", 63))
      {
        jnt->type = RCSJOINT_TRANS_Z;
        jnt->q_min = -1.0;
        jnt->q_max = 1.0;
        jnt->dirIdx = 2;
      }
      else if (STRNCASEEQ(buf, "Xrotation", 63))
      {
        jnt->type = RCSJOINT_ROT_X;
        jnt->q_min = -M_PI;
        jnt->q_max = M_PI;
        jnt->dirIdx = 0;
      }
      else if (STRNCASEEQ(buf, "Yrotation", 63))
      {
        jnt->type = RCSJOINT_ROT_Y;
        jnt->q_min = -M_PI;
        jnt->q_max = M_PI;
        jnt->dirIdx = 1;
      }
      else if (STRNCASEEQ(buf, "Zrotation", 63))
      {
        jnt->type = RCSJOINT_ROT_Z;
        jnt->q_min = -M_PI;
        jnt->q_max = M_PI;
        jnt->dirIdx = 2;
      }
      else
      {
        RLOG(1, "Unknown direction \"%s\" of CHANNELS", buf);
        return false;
      }

      RcsGraph_insertJoint(self, body, jnt);
    }

    fscanf(fd, "%63s", buf);   // Next keyword
    RLOG(5, "Recursing after CHANNELS with next keyword %s", buf);
    parseRecursive(buf, self, body, fd, Vec3d_zeroVec(), linearScaleToSI,
                   Z_up_x_forward);
  }
  else if (STRCASEEQ(buf, "JOINT"))
  {
    fscanf(fd, "%63s", buf);   // Joint link name

    // Create a new body and recursively call this function again
    RcsBody* child = RALLOC(RcsBody);
    child->A_BI = HTr_create();
    child->name = String_clone(buf);
    child->Inertia = HTr_create();
    Mat3d_setZero(child->Inertia->rot);
    RcsBody_addShape(child, createFrameShape(0.1));
    RcsGraph_insertBody(self, body, child);

    fscanf(fd, "%63s", buf);   // Opening curly brace
    fscanf(fd, "%63s", buf);   // Next keyword
    RLOG(5, "Recursing after OFFSET with next keyword %s", buf);
    bool success = parseRecursive(buf, self, child, fd, Vec3d_zeroVec(),
                           linearScaleToSI, Z_up_x_forward);
    RCHECK(success);
    fscanf(fd, "%63s", buf);   // Closing curly brace
  }
  else if (STRCASEEQ(buf, "End"))
  {
    double endOffset[3];
    fscanf(fd, "%63s", buf);  // Site
    fscanf(fd, "%63s", buf);  // {
    fscanf(fd, "%63s", buf);  // OFFSET
    fscanf(fd, "%lf", &endOffset[0]);  // x
    fscanf(fd, "%lf", &endOffset[1]);  // y
    fscanf(fd, "%lf", &endOffset[2]);  // z
    fscanf(fd, "%63s", buf);  // }
    RCHECK(STREQ(buf,"}"));
    fscanf(fd, "%63s", buf);   // Next keyword

    // Sphere at parent origin
    Vec3d_constMulSelf(endOffset, linearScaleToSI);
    double len = 0.8*Vec3d_getLength(endOffset);

    if (len < 0.01)
      {
        len = 0.01;
      }

    RcsShape* shape = RALLOC(RcsShape);
    HTr_setIdentity(&shape->A_CB);
    shape->scale = 1.0;
    shape->type = RCSSHAPE_SPHERE;
    shape->computeType |= RCSSHAPE_COMPUTE_GRAPHICS;
    shape->extents[0] = 0.1*len;
    shape->extents[1] = 0.1*len;
    shape->extents[2] = 0.1*len;
    shape->color = String_clone("BLACK_RUBBER");
    RcsBody_addShape(body, shape);


    RLOG(5, "Recursing after END SITE with next keyword %s", buf);
    parseRecursive(buf, self, body, fd, Vec3d_zeroVec(), linearScaleToSI,
                   Z_up_x_forward);
  }


  RLOG(5, "Reched end of recursion with next keyword %s", buf);

  if (STREQ(buf, "}"))
  {
    return true;
  }
  else if (STREQ(buf, "MOTION"))
  {
    return true;
  }
  else if (STREQ(buf, "Frames:"))
  {
    return true;
  }
  else if (STREQ(buf, "Frame"))   // "Frame Time:"
  {
    return true;
  }
  else
  {
    parseRecursive(buf, self, body, fd, Vec3d_zeroVec(), linearScaleToSI,
                   Z_up_x_forward);
  }

  return true;
}

/*******************************************************************************
 *
 ******************************************************************************/
static void addGeometry(RcsGraph* self)
{

  RCSGRAPH_TRAVERSE_BODIES(self)
  {
    RcsBody* CHILD = BODY->firstChild;

    int rr = Math_getRandomInteger(0, 255);
    int gg = Math_getRandomInteger(0, 255);
    int bb = Math_getRandomInteger(0, 255);
    char color[256];
    sprintf(color, "#%02x%02x%02xff", rr, gg, bb);


    while (CHILD!=NULL)
    {
      RLOG(5, "%s: Traversing child %s", BODY->name, CHILD->name);

      const double* I_p1 = BODY->A_BI->org;
      const double* I_p2 = CHILD->A_BI->org;

      double K_p1[3], K_p2[3], K_p12[3], K_center[3];
      Vec3d_invTransform(K_p1, BODY->A_BI,I_p1);
      Vec3d_invTransform(K_p2, BODY->A_BI,I_p2);
      Vec3d_sub(K_p12, K_p2, K_p1);
      Vec3d_constMulAndAdd(K_center, K_p1, K_p12, 0.5);
      double len = 0.8*Vec3d_getLength(K_p12);

      if (len < 0.01)
        {
          len = 0.01;
        }

      // Box from parent to child
      RcsShape* shape = RALLOC(RcsShape);
      HTr_setIdentity(&shape->A_CB);
      shape->scale = 1.0;
      shape->type = RCSSHAPE_BOX;
      shape->computeType |= RCSSHAPE_COMPUTE_GRAPHICS;
      shape->extents[0] = 0.2*len;
      shape->extents[1] = 0.2*len;
      shape->extents[2] = len;
      shape->color = String_clone(color);
      Mat3d_fromVec(shape->A_CB.rot, K_p12, 2);
      Vec3d_copy(shape->A_CB.org, K_center);
      RcsBody_addShape(BODY, shape);

      // Sphere at parent origin
      shape = RALLOC(RcsShape);
      HTr_setIdentity(&shape->A_CB);
      shape->scale = 1.0;
      shape->type = RCSSHAPE_SPHERE;
      shape->computeType |= RCSSHAPE_COMPUTE_GRAPHICS;
      shape->extents[0] = 0.15*len;
      shape->extents[1] = 0.15*len;
      shape->extents[2] = 0.15*len;
      shape->color = String_clone(color);
      RcsBody_addShape(BODY, shape);

      CHILD=CHILD->next;
    }
  }

}

/*******************************************************************************
 * See header.
 ******************************************************************************/
RcsGraph* RcsGraph_createFromBVHFile(const char* fileName,
                                     double linearScaleToSI,
                                     bool Z_up_x_forward)
{
  FILE* fd = fopen(fileName, "r");

  if (fd==NULL)
  {
    RLOG(1, "Error opening BVH file \"%s\"", fileName);
    return NULL;
  }

  char buf[64] = "";
  bool success = false;

  // First entry must be "HIERARCHY"
  fscanf(fd, "%31s", buf);
  RCHECK(STRCASEEQ(buf, "HIERARCHY"));

  // Second entry must be "ROOT"
  fscanf(fd, "%31s", buf);
  RCHECK(STRCASEEQ(buf, "ROOT"));


  // Create an empty graph that will be propagated recursively
  RcsGraph* self = RALLOC(RcsGraph);
  RCHECK(self);
  self->xmlFile = String_clone(fileName);
  RcsBody* bvhRoot = self->root;

  if (Z_up_x_forward == true)
    {
      RcsBody* xyzRoot = RALLOC(RcsBody);
      xyzRoot->A_BI = HTr_create();
      xyzRoot->name = String_clone("BVHROOT");
      xyzRoot->Inertia = HTr_create();
      xyzRoot->A_BP = HTr_create();
      Mat3d_fromEulerAngles2(xyzRoot->A_BP->rot, M_PI_2, M_PI_2, 0.0);
      Mat3d_setZero(xyzRoot->Inertia->rot);
      RcsBody_addShape(xyzRoot, createFrameShape(1.0));
      RcsGraph_insertBody(self, NULL, xyzRoot);
      bvhRoot = xyzRoot;
    }

  // Start recursion with root link
  success = parseRecursive(buf, self, bvhRoot, fd, Vec3d_zeroVec(),
                           linearScaleToSI, Z_up_x_forward);
  RCHECK(success);

  fclose(fd);

  RcsGraph_setState(self, NULL, NULL);

  addGeometry(self);

  RLOG(5, "Reached end");

  return self;
}

/*******************************************************************************
 * See header.
 ******************************************************************************/
MatNd* RcsGraph_createTrajectoryFromBVHFile(const RcsGraph* graph,
                                            const char* fileName,
                                            double* dt,
                                            double linearScaleToSI,
                                            double angularScaleToSI)
{
  FILE* fd = fopen(fileName, "r");

  if (fd==NULL)
  {
    RLOG(1, "Error opening BVH file \"%s\"", fileName);
    return NULL;
  }


  bool success = findKeyword("MOTION", fd);

  if (success==false)
  {
    RLOG(1, "Couldn't find MOTION keyword - giving up");
    fclose(fd);
    return NULL;
  }

  char buf[64] = "";
  fscanf(fd, "%63s", buf);
  RCHECK(STRCASEEQ(buf, "Frames:"));

  int numFrames = 0;
  fscanf(fd, "%d", &numFrames);
  RLOG(5, "Trajectory has %d frames", numFrames);

  fscanf(fd, "%63s", buf);
  RCHECK_MSG(STRCASEEQ(buf, "Frame"), "%s", buf);

  fscanf(fd, "%63s", buf);
  RCHECK_MSG(STRCASEEQ(buf, "Time:"), "%s", buf);

  double frameTime = 0.0;
  fscanf(fd, "%lf", &frameTime);
  RLOG(5, "Trajectory has frameTime %f", frameTime);

  if (dt!=NULL)
  {
    *dt = frameTime;
  }

  fpos_t trajPos;
  int res = fgetpos(fd, &trajPos);
  unsigned int numValues = 0;
  RCHECK(res==0);
  int isEOF = 0;

  do
  {
    double dummy;
    isEOF = fscanf(fd, "%lf", &dummy);
    numValues++;
  }
  while (isEOF != EOF);

  RLOG(5, "Found %d values", numValues);
  numValues--;


  if (numValues%numFrames!=0)
  {
    RLOG(4, "Modulo is %d but should be 0", numValues % numFrames);
    fclose(fd);
    return NULL;
  }

  res = fsetpos(fd, &trajPos);

  RLOG(5, "Creating %d x %d array", numFrames, (int)numValues/numFrames);
  MatNd* data = MatNd_create(numFrames, (int)numValues/numFrames);

  RCHECK(graph->dof*numFrames==numValues);

  numValues = 0;
  isEOF = 0;
  do
  {

    isEOF = fscanf(fd, "%lf", &data->ele[numValues]);
    numValues++;
  }
  while (isEOF != EOF);



  fclose(fd);

  MatNd* scaleArr = MatNd_create(1, graph->dof);
  RCSGRAPH_TRAVERSE_JOINTS(graph)
  {
    scaleArr->ele[JNT->jointIndex] = RcsJoint_isRotation(JNT) ? angularScaleToSI : linearScaleToSI;
  }

  for (unsigned int i=0; i<data->m; ++i)
    {
      MatNd row = MatNd_getRowView(data, i);
      MatNd_eleMulSelf(&row, scaleArr);
    }

  MatNd_destroy(scaleArr);

  return data;
}