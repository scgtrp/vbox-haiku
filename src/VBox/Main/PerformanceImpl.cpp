/* $Id$ */

/** @file
 *
 * VBox Performance API COM Classes implementation
 */

/*
 * Copyright (C) 2008 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#if defined(RT_OS_WINDOWS)
#elif defined(RT_OS_LINUX)
#endif

#include "PerformanceImpl.h"

#include "Logging.h"

#include <VBox/err.h>
#include <iprt/process.h>

#include <vector>
#include <algorithm>
#include <functional>

static Bstr gMetricNames[] =
{
    "CPU/Load/User:avg",
    "CPU/Load/User:min",
    "CPU/Load/User:max",
    "CPU/Load/Kernel:avg",
    "CPU/Load/Kernel:min",
    "CPU/Load/Kernel:max",
    "CPU/Load/Idle:avg",
    "CPU/Load/Idle:min",
    "CPU/Load/Idle:max",
    "CPU/MHz:avg",
    "CPU/MHz:min",
    "CPU/MHz:max",
    "RAM/Usage/Total:avg",
    "RAM/Usage/Total:min",
    "RAM/Usage/Total:max",
    "RAM/Usage/Used:avg",
    "RAM/Usage/Used:min",
    "RAM/Usage/Used:max",
    "RAM/Usage/Free:avg",
    "RAM/Usage/Free:min",
    "RAM/Usage/Free:max",
};

////////////////////////////////////////////////////////////////////////////////
// PerformanceCollector class
////////////////////////////////////////////////////////////////////////////////

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

PerformanceCollector::PerformanceCollector() : mMagic(0) {}

PerformanceCollector::~PerformanceCollector() {}

HRESULT PerformanceCollector::FinalConstruct()
{
    LogFlowThisFunc (("\n"));

    return S_OK;
}

void PerformanceCollector::FinalRelease()
{
    LogFlowThisFunc (("\n"));
}

// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the PerformanceCollector object.
 */
HRESULT PerformanceCollector::init()
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan (this);
    AssertReturn (autoInitSpan.isOk(), E_UNEXPECTED);

    LogFlowThisFuncEnter();

    HRESULT rc = S_OK;

    /* @todo Obviously other platforms must be added as well. */
    m.mFactory = new pm::MetricFactoryLinux();

    /* Let the sampler know it gets a valid collector.  */
    mMagic = MAGIC;

    /* Start resource usage sampler */
    int vrc = RTTimerCreate (&m.mSampler, VBOX_USAGE_SAMPLER_MIN_INTERVAL,
                             &PerformanceCollector::staticSamplerCallback, this);
    AssertMsgRC (vrc, ("Failed to create resource usage "
                       "sampling timer(%Rra)\n", vrc));
    if (RT_FAILURE (vrc))
        rc = E_FAIL;

    if (SUCCEEDED (rc))
        autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();

    return rc;
}

/**
 * Uninitializes the PerformanceCollector object.
 *
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void PerformanceCollector::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan (this);
    if (autoUninitSpan.uninitDone())
    {
        LogFlowThisFunc (("Already uninitialized.\n"));
        LogFlowThisFuncLeave();
        return;
    }

    mMagic = 0;

    /* Destroy resource usage sampler */
    int vrc = RTTimerDestroy (m.mSampler);
    AssertMsgRC (vrc, ("Failed to destroy resource usage "
                       "sampling timer (%Rra)\n", vrc));
    m.mSampler = NULL;

    delete m.mFactory;
    m.mFactory = NULL;

    LogFlowThisFuncLeave();
}

// IPerformanceCollector properties
////////////////////////////////////////////////////////////////////////////////

STDMETHODIMP
PerformanceCollector::COMGETTER(MetricNames) (ComSafeArrayOut (BSTR, theMetricNames))
{
    if (ComSafeArrayOutIsNull (theMetricNames))
        return E_POINTER;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    AutoReadLock alock (this);

    com::SafeArray <BSTR> metricNames(RT_ELEMENTS(gMetricNames));
    for (size_t i = 0; i < RT_ELEMENTS(gMetricNames); i++)
    {
        gMetricNames[i].detachTo(&metricNames[i]);
    }
    //gMetricNames.detachTo(ComSafeArrayOutArg (theMetricNames));
    metricNames.detachTo (ComSafeArrayOutArg (theMetricNames));

    return S_OK;
}

// IPerformanceCollector methods
////////////////////////////////////////////////////////////////////////////////

STDMETHODIMP PerformanceCollector::GetMetrics(ComSafeArrayIn(INPTR BSTR, metricNames),
                                              ComSafeArrayIn(IUnknown *, objects),
                                              ComSafeArrayOut(IPerformanceMetric *, outMetrics))
{
    //LogFlowThisFunc (("mState=%d, mType=%d\n", mState, mType));

    HRESULT rc = S_OK;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    pm::Filter filter(ComSafeArrayInArg(metricNames), ComSafeArrayInArg(objects));

    MetricList filteredMetrics;
    MetricList::iterator it;
    for (it = m.mMetrics.begin(); it != m.mMetrics.end(); ++it)
        if (filter.match((*it)->getObject(), (*it)->getName()))
            filteredMetrics.push_back(*it);

    com::SafeIfaceArray<IPerformanceMetric> retMetrics(filteredMetrics.size());
    int i = 0;
    for (it = m.mMetrics.begin(); it != m.mMetrics.end(); ++it)
    {
        ComObjPtr<PerformanceMetric> metric;
        rc = metric.createObject();
        if (SUCCEEDED (rc))
            rc = metric->init (*it);
        ComAssertComRCThrowRC (rc);
        metric.queryInterfaceTo(&retMetrics[i++]);
    }
    retMetrics.detachTo(ComSafeArrayOutArg(outMetrics));
    return rc;
}

STDMETHODIMP PerformanceCollector::SetupMetrics (ComSafeArrayIn(INPTR BSTR, metricNames),
                                                 ComSafeArrayIn(IUnknown *, objects),
                                                 ULONG aPeriod, ULONG aCount)
{
    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    pm::Filter filter(ComSafeArrayInArg(metricNames), ComSafeArrayInArg(objects));

    BaseMetricList::iterator it;
    for (it = m.mBaseMetrics.begin(); it != m.mBaseMetrics.end(); ++it)
        if (filter.match((*it)->getObject(), (*it)->getName()))
        {
            (*it)->init(aPeriod, aCount);
            (*it)->enable();
        }

    return S_OK;
}

STDMETHODIMP PerformanceCollector::EnableMetrics (ComSafeArrayIn(INPTR BSTR, metricNames),
                                                  ComSafeArrayIn(IUnknown *, objects))
{
    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    pm::Filter filter(ComSafeArrayInArg(metricNames), ComSafeArrayInArg(objects));

    BaseMetricList::iterator it;
    for (it = m.mBaseMetrics.begin(); it != m.mBaseMetrics.end(); ++it)
        if (filter.match((*it)->getObject(), (*it)->getName()))
            (*it)->enable();

    return S_OK;
}

STDMETHODIMP PerformanceCollector::DisableMetrics (ComSafeArrayIn(INPTR BSTR, metricNames),
                                                   ComSafeArrayIn(IUnknown *, objects))
{
    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    pm::Filter filter(ComSafeArrayInArg(metricNames), ComSafeArrayInArg(objects));

    BaseMetricList::iterator it;
    for (it = m.mBaseMetrics.begin(); it != m.mBaseMetrics.end(); ++it)
        if (filter.match((*it)->getObject(), (*it)->getName()))
            (*it)->disable();

    return S_OK;
}

STDMETHODIMP PerformanceCollector::QueryMetricsData (ComSafeArrayIn(INPTR BSTR, metricNames),
                                                     ComSafeArrayIn(IUnknown *, objects),
                                                     ComSafeArrayOut(BSTR, outMetricNames),
                                                     ComSafeArrayOut(IUnknown *, outObjects),
                                                     ComSafeArrayOut(ULONG, outDataIndices),
                                                     ComSafeArrayOut(ULONG, outDataLengths),
                                                     ComSafeArrayOut(LONG, outData))
{
    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    /// @todo r=dmik don't we need to lock this for reading?

    int i;
    MetricList::const_iterator it;
    /* Let's compute the size of the resulting flat array */
    size_t flatSize = 0, numberOfMetrics = 0;
    for (it = m.mMetrics.begin(); it != m.mMetrics.end(); ++it)
    {
        /* @todo Filtering goes here! */
        flatSize += (*it)->getLength();
        ++numberOfMetrics;
    }
    size_t flatIndex = 0;
    com::SafeArray<BSTR> retNames(numberOfMetrics);
    com::SafeIfaceArray<IUnknown> retObjects(numberOfMetrics);
    com::SafeArray<ULONG> retIndices(numberOfMetrics);
    com::SafeArray<ULONG> retLengths(numberOfMetrics);
    com::SafeArray<LONG> retData(flatSize);

    for (it = m.mMetrics.begin(), i = 0; it != m.mMetrics.end(); ++it)
    {
        /* @todo Filtering goes here! */
        unsigned long *values, length;
        /* @todo We may want to revise the query method to get rid of excessive alloc/memcpy calls. */
        (*it)->query(&values, &length);
        memcpy(retData.raw() + flatIndex, values, length * sizeof(*values));
        Bstr tmp((*it)->getName());
        tmp.detachTo(&retNames[i]);
        (*it)->getObject().queryInterfaceTo(&retObjects[i]);
        retLengths[i] = length;
        retIndices[i] = flatIndex;
        ++i;
        flatIndex += length;
    }

    retNames.detachTo(ComSafeArrayOutArg(outMetricNames));
    retObjects.detachTo(ComSafeArrayOutArg(outObjects));
    retIndices.detachTo(ComSafeArrayOutArg(outDataIndices));
    retLengths.detachTo(ComSafeArrayOutArg(outDataLengths));
    retData.detachTo(ComSafeArrayOutArg(outData));
    return S_OK;
}

// public methods for internal purposes
///////////////////////////////////////////////////////////////////////////////

void PerformanceCollector::registerBaseMetric (pm::BaseMetric *baseMetric)
{
    m.mBaseMetrics.push_back (baseMetric);
}

void PerformanceCollector::registerMetric (pm::Metric *metric)
{
    m.mMetrics.push_back (metric);
}

void PerformanceCollector::unregisterBaseMetricsFor (const ComPtr <IUnknown> &aObject)
{
    std::remove_if (m.mBaseMetrics.begin(), m.mBaseMetrics.end(),
                    std::bind2nd (std::mem_fun (&pm::BaseMetric::associatedWith),
                                  aObject));
}

void PerformanceCollector::unregisterMetricsFor (const ComPtr <IUnknown> &aObject)
{
    std::remove_if (m.mMetrics.begin(), m.mMetrics.end(),
                    std::bind2nd (std::mem_fun (&pm::Metric::associatedWith),
                                  aObject));
}

// private methods
///////////////////////////////////////////////////////////////////////////////

/* static */
void PerformanceCollector::staticSamplerCallback (PRTTIMER pTimer, void *pvUser,
                                                  uint64_t iTick)
{
    AssertReturnVoid (pvUser != NULL);
    PerformanceCollector *collector = static_cast <PerformanceCollector *> (pvUser);
    Assert(collector->mMagic == MAGIC);
    if (collector->mMagic == MAGIC)
    {
        collector->samplerCallback();
    }
}

void PerformanceCollector::samplerCallback()
{
    uint64_t timestamp = RTTimeMilliTS();
    std::for_each(m.mBaseMetrics.begin(), m.mBaseMetrics.end(),
                  std::bind2nd (std::mem_fun (&pm::BaseMetric::collectorBeat), timestamp));
}

////////////////////////////////////////////////////////////////////////////////
// PerformanceMetric class
////////////////////////////////////////////////////////////////////////////////

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

PerformanceMetric::PerformanceMetric()
{
    mMetric = 0;
}

PerformanceMetric::~PerformanceMetric()
{
}

HRESULT PerformanceMetric::FinalConstruct()
{
    LogFlowThisFunc (("\n"));

    return S_OK;
}

void PerformanceMetric::FinalRelease()
{
    LogFlowThisFunc (("\n"));

    uninit ();
}

// public initializer/uninitializer for internal purposes only
////////////////////////////////////////////////////////////////////////////////

HRESULT PerformanceMetric::init (pm::Metric *aMetric)
{
    mMetric = aMetric;
    return S_OK;
}

void PerformanceMetric::uninit()
{
}

STDMETHODIMP PerformanceMetric::COMGETTER(MetricName) (BSTR *aMetricName)
{
    Bstr tmp (mMetric->getName());
    tmp.detachTo (aMetricName);
    return S_OK;
}

STDMETHODIMP PerformanceMetric::COMGETTER(Object) (IUnknown **anObject)
{
    *anObject = mMetric->getObject();
    return S_OK;
}

STDMETHODIMP PerformanceMetric::COMGETTER(Period) (ULONG *aPeriod)
{
    *aPeriod = mMetric->getPeriod();
    return S_OK;
}

STDMETHODIMP PerformanceMetric::COMGETTER(Count) (ULONG *aCount)
{
    *aCount = mMetric->getLength();
    return S_OK;
}

STDMETHODIMP PerformanceMetric::COMGETTER(Unit) (BSTR *aUnit)
{
    Bstr tmp(mMetric->getUnit());
    tmp.detachTo(aUnit);
    return S_OK;
}

STDMETHODIMP PerformanceMetric::COMGETTER(MinimumValue) (LONG *aMinValue)
{
    *aMinValue = mMetric->getMinValue();
    return S_OK;
}

STDMETHODIMP PerformanceMetric::COMGETTER(MaximumValue) (LONG *aMaxValue)
{
    *aMaxValue = mMetric->getMaxValue();
    return S_OK;
}

