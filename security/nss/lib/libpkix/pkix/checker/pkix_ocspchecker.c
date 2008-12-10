/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the PKIX-C library.
 *
 * The Initial Developer of the Original Code is
 * Sun Microsystems, Inc.
 * Portions created by the Initial Developer are
 * Copyright 2004-2007 Sun Microsystems, Inc.  All Rights Reserved.
 *
 * Contributor(s):
 *   Sun Microsystems, Inc.
 *   Red Hat, Inc.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
/*
 * pkix_ocspchecker.c
 *
 * OcspChecker Object Functions
 *
 */

#include "pkix_ocspchecker.h"
#include "pkix_pl_ocspcertid.h"
#include "pkix_error.h"


/* --Private-Data-and-Types--------------------------------------- */

typedef struct pkix_OcspCheckerStruct {
    /* RevocationMethod is the super class of OcspChecker. */
    pkix_RevocationMethod method;
    PKIX_PL_VerifyCallback certVerifyFcn;
} pkix_OcspChecker;

/* --Private-Functions-------------------------------------------- */

/*
 * FUNCTION: pkix_OcspChecker_Destroy
 *      (see comments for PKIX_PL_DestructorCallback in pkix_pl_system.h)
 */
static PKIX_Error *
pkix_OcspChecker_Destroy(
        PKIX_PL_Object *object,
        void *plContext)
{
    return NULL;
}

/*
 * FUNCTION: pkix_OcspChecker_RegisterSelf
 * DESCRIPTION:
 *  Registers PKIX_OCSPCHECKER_TYPE and its related functions with
 *  systemClasses[]
 * THREAD SAFETY:
 *  Not Thread Safe - for performance and complexity reasons
 *
 *  Since this function is only called by PKIX_PL_Initialize, which should
 *  only be called once, it is acceptable that this function is not
 *  thread-safe.
 */
PKIX_Error *
pkix_OcspChecker_RegisterSelf(void *plContext)
{
        extern pkix_ClassTable_Entry systemClasses[PKIX_NUMTYPES];
        pkix_ClassTable_Entry* entry = &systemClasses[PKIX_OCSPCHECKER_TYPE];

        PKIX_ENTER(OCSPCHECKER, "pkix_OcspChecker_RegisterSelf");

        entry->description = "OcspChecker";
        entry->typeObjectSize = sizeof(pkix_OcspChecker);
        entry->destructor = pkix_OcspChecker_Destroy;

        PKIX_RETURN(OCSPCHECKER);
}


/*
 * FUNCTION: pkix_OcspChecker_Create
 */
PKIX_Error *
pkix_OcspChecker_Create(PKIX_RevocationMethodType methodType,
                        PKIX_UInt32 flags,
                        PKIX_UInt32 priority,
                        pkix_LocalRevocationCheckFn localRevChecker,
                        pkix_ExternalRevocationCheckFn externalRevChecker,
                        PKIX_PL_VerifyCallback verifyFn,
                        pkix_RevocationMethod **pChecker,
                        void *plContext)
{
        pkix_OcspChecker *method = NULL;

        PKIX_ENTER(OCSPCHECKER, "pkix_OcspChecker_Create");
        PKIX_NULLCHECK_ONE(pChecker);

        PKIX_CHECK(PKIX_PL_Object_Alloc
                    (PKIX_OCSPCHECKER_TYPE,
                    sizeof (pkix_OcspChecker),
                    (PKIX_PL_Object **)&method,
                    plContext),
                    PKIX_COULDNOTCREATECERTCHAINCHECKEROBJECT);

        pkixErrorResult = pkix_RevocationMethod_Init(
            (pkix_RevocationMethod*)method, methodType, flags,  priority,
            localRevChecker, externalRevChecker, plContext);
        if (pkixErrorResult) {
            goto cleanup;
        }
        method->certVerifyFcn = (PKIX_PL_VerifyCallback)verifyFn;

        *pChecker = (pkix_RevocationMethod*)method;
        method = NULL;

cleanup:
        PKIX_DECREF(method);

        PKIX_RETURN(OCSPCHECKER);
}

/* --Public-Functions--------------------------------------------- */

/*
 * FUNCTION: pkix_OcspChecker_Check (see comments in pkix_checker.h)
 */

/*
 * The OCSPChecker is created in an idle state, and remains in this state until
 * either (a) the default Responder has been set and enabled, and a Check
 * request is received with no responder specified, or (b) a Check request is
 * received with a specified responder. A request message is constructed and
 * given to the HttpClient. If non-blocking I/O is used the client may return
 * with WOULDBLOCK, in which case the OCSPChecker returns the WOULDBLOCK
 * condition to its caller in turn. On a subsequent call the I/O is resumed.
 * When a response is received it is decoded and the results provided to the
 * caller.
 *
 */
PKIX_Error *
pkix_OcspChecker_CheckLocal(
        PKIX_PL_Cert *cert,
        PKIX_PL_Cert *issuer,
        PKIX_PL_Date *date,
        pkix_RevocationMethod *checkerObject,
        PKIX_ProcessingParams *procParams,
        PKIX_UInt32 methodFlags,
        PKIX_RevocationStatus *pRevStatus,
        PKIX_UInt32 *pReasonCode,
        void *plContext)
{
        PKIX_PL_OcspCertID    *cid = NULL;
        PKIX_Boolean           hasFreshStatus = PKIX_FALSE;
        PKIX_Boolean           statusIsGood = PKIX_FALSE;
        SECErrorCodes          resultCode = SEC_ERROR_REVOKED_CERTIFICATE_OCSP;
        PKIX_RevocationStatus  revStatus = PKIX_RevStatus_NoInfo;

        PKIX_ENTER(OCSPCHECKER, "pkix_OcspChecker_CheckLocal");

        PKIX_CHECK(
            PKIX_PL_OcspCertID_Create(cert, NULL, &cid,
                                      plContext),
            PKIX_OCSPCERTIDCREATEFAILED);
        if (!cid) {
            goto cleanup;
        }

        PKIX_CHECK(
            PKIX_PL_OcspCertID_GetFreshCacheStatus(cid, NULL,
                                                   &hasFreshStatus,
                                                   &statusIsGood,
                                                   &resultCode,
                                                   plContext),
            PKIX_OCSPCERTIDGETFRESHCACHESTATUSFAILED);
        if (hasFreshStatus) {
            if (statusIsGood) {
                revStatus = PKIX_RevStatus_Success;
                resultCode = 0;
            } else {
                revStatus = PKIX_RevStatus_Revoked;
            }
        }

cleanup:
        *pRevStatus = revStatus;
        PKIX_DECREF(cid);

        PKIX_RETURN(OCSPCHECKER);
}


/*
 * The OCSPChecker is created in an idle state, and remains in this state until
 * either (a) the default Responder has been set and enabled, and a Check
 * request is received with no responder specified, or (b) a Check request is
 * received with a specified responder. A request message is constructed and
 * given to the HttpClient. If non-blocking I/O is used the client may return
 * with WOULDBLOCK, in which case the OCSPChecker returns the WOULDBLOCK
 * condition to its caller in turn. On a subsequent call the I/O is resumed.
 * When a response is received it is decoded and the results provided to the
 * caller.
 *
 */
PKIX_Error *
pkix_OcspChecker_CheckExternal(
        PKIX_PL_Cert *cert,
        PKIX_PL_Cert *issuer,
        PKIX_PL_Date *date,
        pkix_RevocationMethod *checkerObject,
        PKIX_ProcessingParams *procParams,
        PKIX_UInt32 methodFlags,
        PKIX_RevocationStatus *pRevStatus,
        PKIX_UInt32 *pReasonCode,
        void **pNBIOContext,
        void *plContext)
{
        SECErrorCodes resultCode = SEC_ERROR_REVOKED_CERTIFICATE_OCSP;
        PKIX_Boolean uriFound = PKIX_FALSE;
        PKIX_Boolean passed = PKIX_TRUE;
        pkix_OcspChecker *checker = NULL;
        PKIX_PL_OcspCertID *cid = NULL;
        PKIX_PL_OcspRequest *request = NULL;
        PKIX_PL_OcspResponse *response = NULL;
        PKIX_PL_Date *validity = NULL;
        PKIX_RevocationStatus revStatus = PKIX_RevStatus_NoInfo;
        void *nbioContext = NULL;

        PKIX_ENTER(OCSPCHECKER, "pkix_OcspChecker_Check");

        PKIX_CHECK(
            pkix_CheckType((PKIX_PL_Object*)checkerObject,
                           PKIX_OCSPCHECKER_TYPE, plContext),
                PKIX_OBJECTNOTOCSPCHECKER);

        checker = (pkix_OcspChecker *)checkerObject;

        PKIX_CHECK(
            PKIX_PL_OcspCertID_Create(cert, NULL, &cid,
                                      plContext),
            PKIX_OCSPCERTIDCREATEFAILED);
        
        /* create request */
        PKIX_CHECK(
            pkix_pl_OcspRequest_Create(cert, cid, validity, NULL, 
                                       methodFlags, &uriFound, &request,
                                       plContext),
            PKIX_OCSPREQUESTCREATEFAILED);
        
        if (uriFound == PKIX_FALSE) {
            /* no caching for certs lacking URI */
            resultCode = 0;
            if (methodFlags & PKIX_REV_M_REQUIRE_INFO_ON_MISSING_SOURCE) {
                revStatus = PKIX_RevStatus_Revoked;
            }
            goto cleanup;
        }

        /* send request and create a response object */
        PKIX_CHECK(
            pkix_pl_OcspResponse_Create(request, NULL,
                                        checker->certVerifyFcn,
                                        &nbioContext,
                                        &response,
                                        plContext),
            PKIX_OCSPRESPONSECREATEFAILED);
        if (nbioContext != 0) {
            *pNBIOContext = nbioContext;
            goto cleanup;
        }
        
        PKIX_CHECK(
            pkix_pl_OcspResponse_Decode(response, &passed,
                                        &resultCode, plContext),
            PKIX_OCSPRESPONSEDECODEFAILED);
        if (passed == PKIX_FALSE) {
            goto cleanup;
        }
        
        PKIX_CHECK(
            pkix_pl_OcspResponse_GetStatus(response, &passed,
                                           &resultCode, plContext),
            PKIX_OCSPRESPONSEGETSTATUSRETURNEDANERROR);
        if (passed == PKIX_FALSE) {
            goto cleanup;
        }

        PKIX_CHECK(
            pkix_pl_OcspResponse_VerifySignature(response, cert,
                                                 procParams, &passed, 
                                                 &nbioContext, plContext),
            PKIX_OCSPRESPONSEVERIFYSIGNATUREFAILED);
       	if (nbioContext != 0) {
               	*pNBIOContext = nbioContext;
                goto cleanup;
        }
        if (passed == PKIX_FALSE) {
                resultCode = PORT_GetError();
                goto cleanup;
        }

        PKIX_CHECK(
            pkix_pl_OcspResponse_GetStatusForCert(cid, response,
                                                  &passed, &resultCode,
                                                  plContext),
            PKIX_OCSPRESPONSEGETSTATUSFORCERTFAILED);
        if (passed == PKIX_FALSE) {
            revStatus = PKIX_RevStatus_Revoked;
        } else {
            revStatus = PKIX_RevStatus_Success;
        }

cleanup:
        if (revStatus == PKIX_RevStatus_NoInfo && uriFound &&
            methodFlags & PKIX_REV_M_FAIL_ON_MISSING_FRESH_INFO) {
            revStatus = PKIX_RevStatus_Revoked;
        }
        *pRevStatus = revStatus;

        if (!passed && cid && cid->certID) {
                /* We still own the certID object, which means that 
                 * it did not get consumed to create a cache entry.
                 * Let's make sure there is one.
                 */
                PKIX_Error *err;
                err = PKIX_PL_OcspCertID_RememberOCSPProcessingFailure(
                        cid, plContext);
                if (err) {
                        PKIX_PL_Object_DecRef((PKIX_PL_Object*)err, plContext);
                }
        }
        PKIX_DECREF(cid);
        PKIX_DECREF(request);
        PKIX_DECREF(response);

        PKIX_RETURN(OCSPCHECKER);
}

