/*
  eXosip - This is the eXtended osip library.
  Copyright (C) 2001-2012 Aymeric MOIZARD amoizard@antisip.com
  
  eXosip is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  eXosip is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  In addition, as a special exception, the copyright holders give
  permission to link the code of portions of this program with the
  OpenSSL library under certain conditions as described in each
  individual source file, and distribute linked combinations
  including the two.
  You must obey the GNU General Public License in all respects
  for all of the code used other than OpenSSL.  If you modify
  file(s) with this exception, you may extend this exception to your
  version of the file(s), but you are not obligated to do so.  If you
  do not wish to do so, delete this exception statement from your
  version.  If you delete this exception statement from all source
  files in the program, then also delete it here.
*/

#include "eXosip2.h"

int
_eXosip_build_response_default (struct eXosip_t *excontext, osip_message_t ** dest, osip_dialog_t * dialog, int status, osip_message_t * request)
{
  osip_generic_param_t *tag;
  osip_message_t *response;
  int pos;
  int i;

  *dest = NULL;
  if (request == NULL)
    return OSIP_BADPARAMETER;

  i = osip_message_init (&response);
  if (i != 0)
    return i;
  /* initialise osip_message_t structure */
  /* yet done... */

  response->sip_version = (char *) osip_malloc (8 * sizeof (char));
  if (response->sip_version == NULL) {
    osip_message_free (response);
    return OSIP_NOMEM;
  }
  sprintf (response->sip_version, "SIP/2.0");
  osip_message_set_status_code (response, status);

#ifndef MINISIZE
  /* handle some internal reason definitions. */
  if (MSG_IS_NOTIFY (request) && status == 481) {
    response->reason_phrase = osip_strdup ("Subscription Does Not Exist");
  }
  else if (MSG_IS_SUBSCRIBE (request) && status == 202) {
    response->reason_phrase = osip_strdup ("Accepted subscription");
  }
  else {
    response->reason_phrase = osip_strdup (osip_message_get_reason (status));
    if (response->reason_phrase == NULL) {
      if (response->status_code == 101)
        response->reason_phrase = osip_strdup ("Dialog Establishement");
      else
        response->reason_phrase = osip_strdup ("Unknown code");
    }
    response->req_uri = NULL;
    response->sip_method = NULL;
  }
#else
  response->reason_phrase = osip_strdup (osip_message_get_reason (status));
  if (response->reason_phrase == NULL) {
    if (response->status_code == 101)
      response->reason_phrase = osip_strdup ("Dialog Establishement");
    else
      response->reason_phrase = osip_strdup ("Unknown code");
  }
  response->req_uri = NULL;
  response->sip_method = NULL;
#endif

  if (response->reason_phrase == NULL) {
    osip_message_free (response);
    return OSIP_NOMEM;
  }

  i = osip_to_clone (request->to, &(response->to));
  if (i != 0) {
    osip_message_free (response);
    return i;
  }

  i = osip_to_get_tag (response->to, &tag);
  if (i != 0) {                 /* we only add a tag if it does not already contains one! */
    if ((dialog != NULL) && (dialog->local_tag != NULL))
      /* it should contain the local TAG we created */
    {
      osip_to_set_tag (response->to, osip_strdup (dialog->local_tag));
    }
    else {
      if (status != 100)
        osip_to_set_tag (response->to, _eXosip_malloc_new_random ());
    }
  }

  i = osip_from_clone (request->from, &(response->from));
  if (i != 0) {
    osip_message_free (response);
    return i;
  }

  pos = 0;
  while (!osip_list_eol (&request->vias, pos)) {
    osip_via_t *via;
    osip_via_t *via2;

    via = (osip_via_t *) osip_list_get (&request->vias, pos);
    i = osip_via_clone (via, &via2);
    if (i != 0) {
      osip_message_free (response);
      return i;
    }
    osip_list_add (&response->vias, via2, -1);
    pos++;
  }

  i = osip_call_id_clone (request->call_id, &(response->call_id));
  if (i != 0) {
    osip_message_free (response);
    return i;
  }
  i = osip_cseq_clone (request->cseq, &(response->cseq));
  if (i != 0) {
    osip_message_free (response);
    return i;
  }
#ifndef MINISIZE
  if (MSG_IS_SUBSCRIBE (request)) {
    osip_header_t *exp;
    osip_header_t *evt_hdr;

    osip_message_header_get_byname (request, "event", 0, &evt_hdr);
    if (evt_hdr != NULL && evt_hdr->hvalue != NULL)
      osip_message_set_header (response, "Event", evt_hdr->hvalue);
    else
      osip_message_set_header (response, "Event", "presence");
    i = osip_message_get_expires (request, 0, &exp);
    if (exp == NULL) {
      osip_header_t *cp;

      i = osip_header_clone (exp, &cp);
      if (cp != NULL)
        osip_list_add (&response->headers, cp, 0);
    }
  }
#endif

  osip_message_set_user_agent (response, excontext->user_agent);

  *dest = response;
  return OSIP_SUCCESS;
}

int
_eXosip_complete_answer_that_establish_a_dialog (struct eXosip_t *excontext, osip_message_t * response, osip_message_t * request)
{
  int i;
  int pos = 0;
  char contact[1024];
  char locip[65];
  char firewall_ip[65];
  char firewall_port[10];
  char scheme[10];

  snprintf(scheme, sizeof(scheme), "sip");

  /* 12.1.1:
     copy all record-route in response
     add a contact with global scope
   */
  while (!osip_list_eol (&request->record_routes, pos)) {
    osip_record_route_t *rr;
    osip_record_route_t *rr2;

    rr = (osip_record_route_t *) osip_list_get (&request->record_routes, pos);
    i = osip_record_route_clone (rr, &rr2);
    if (i != 0)
      return i;
    osip_list_add (&response->record_routes, rr2, -1);

    /* rfc3261: 12.1.1 UAS behavior (check sips in top most Record-Route) */
    if (pos==0 && rr2!=NULL && rr2->url!=NULL && rr2->url->scheme!=NULL && osip_strcasecmp(rr2->url->scheme, "sips")==0)
      snprintf(scheme, sizeof(scheme), "sips");

    pos++;
  }

  if (MSG_IS_BYE (request)) {
    return OSIP_SUCCESS;
  }

  if (pos==0) {
    /* rfc3261: 12.1.1 UAS behavior (check sips in Contact if no Record-Route) */
    osip_contact_t *co = (osip_contact_t *) osip_list_get(&request->contacts, 0);
    if (pos==0 && co!=NULL && co->url!=NULL && co->url->scheme!=NULL && osip_strcasecmp(co->url->scheme, "sips")==0)
      snprintf(scheme, sizeof(scheme), "sips");
  }
  /* rfc3261: 12.1.1 UAS behavior (check sips in Request-URI) */
  if (request->req_uri->scheme!=NULL && osip_strcasecmp(request->req_uri->scheme, "sips")==0)
    snprintf(scheme, sizeof(scheme), "sips");

  firewall_ip[0] = '\0';
  firewall_port[0] = '\0';
  if (excontext->eXtl_transport.tl_get_masquerade_contact != NULL) {
    excontext->eXtl_transport.tl_get_masquerade_contact (excontext, firewall_ip, sizeof (firewall_ip), firewall_port, sizeof (firewall_port));
  }

  memset (locip, '\0', sizeof (locip));
  _eXosip_guess_ip_for_via (excontext, excontext->eXtl_transport.proto_family, locip, 49);

  if (request->to->url->username == NULL)
    snprintf (contact, 1000, "<%s:%s:%s>", scheme, locip, firewall_port);
  else {
    char *tmp2 = __osip_uri_escape_userinfo (request->to->url->username);

    snprintf (contact, 1000, "<%s:%s@%s:%s>", scheme, tmp2, locip, firewall_port);
    osip_free (tmp2);
  }
  if (firewall_ip[0] != '\0') {
#ifdef USE_LOCALIP_WITH_LOCALPROXY      /* disable this code for local testing because it adds an extra DNS */
    osip_contact_t *con = (osip_contact_t *) osip_list_get (&request->contacts, 0);

    if (con != NULL && con->url != NULL && con->url->host != NULL) {
      char *c_address = con->url->host;

      struct addrinfo *addrinfo;
      struct __eXosip_sockaddr addr;

      i = _eXosip_get_addrinfo (excontext, &addrinfo, con->url->host, 5060, IPPROTO_UDP);
      if (i == 0) {
        memcpy (&addr, addrinfo->ai_addr, addrinfo->ai_addrlen);
        _eXosip_freeaddrinfo (addrinfo);
        c_address = inet_ntoa (((struct sockaddr_in *) &addr)->sin_addr);
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "eXosip: here is the resolved destination host=%s\n", c_address));
      }

      /* If c_address is a PUBLIC address, the request was
         coming from the PUBLIC network. */
      if (_eXosip_is_public_address (c_address)) {
        if (request->to->url->username == NULL)
          snprintf (contact, 1000, "<%s:%s:%s>", scheme, firewall_ip, firewall_port);
        else {
          char *tmp2 = __osip_uri_escape_userinfo (request->to->url->username);

          snprintf (contact, 1000, "<%s:%s@%s:%s>", scheme, tmp2, firewall_ip, firewall_port);
          osip_free (tmp2);
        }
      }
    }
#else
    if (request->to->url->username == NULL)
      snprintf (contact, 1000, "<%s:%s:%s>", scheme, firewall_ip, firewall_port);
    else {
      char *tmp2 = __osip_uri_escape_userinfo (request->to->url->username);

      snprintf (contact, 1000, "<%s:%s@%s:%s>", scheme, tmp2, firewall_ip, firewall_port);
      osip_free (tmp2);
    }
#endif
  }

  {
    osip_via_t *via;

    via = (osip_via_t *) osip_list_get (&response->vias, 0);
    if (via == NULL || via->protocol == NULL)
      return OSIP_SYNTAXERROR;
    if (strlen (contact) + strlen (via->protocol) + strlen (";transport=>") < 1024 && 0 != osip_strcasecmp (via->protocol, "UDP")) {
      contact[strlen (contact) - 1] = '\0';
      strcat (contact, ";transport=");
      strcat (contact, via->protocol);
      strcat (contact, ">");
    }
  }

  osip_message_set_contact (response, contact);

  if (excontext->eXtl_transport.tl_update_local_target!=NULL)
    excontext->eXtl_transport.tl_update_local_target(excontext, response);
  return OSIP_SUCCESS;
}

int
_eXosip_answer_invite_123456xx (struct eXosip_t *excontext, eXosip_call_t * jc, eXosip_dialog_t * jd, int code, osip_message_t ** answer, int send)
{
  int i;
  osip_transaction_t *tr;

  *answer = NULL;
  tr = _eXosip_find_last_inc_invite (jc, jd);

  if (tr == NULL || tr->orig_request == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: cannot find transaction to answer\n"));
    return OSIP_NOTFOUND;
  }

  if (code >= 200 && code < 300 && jd != NULL && jd->d_dialog == NULL) {        /* element previously removed */
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: cannot answer this closed transaction\n"));
    return OSIP_WRONG_STATE;
  }

  /* is the transaction already answered? */
  if (tr->state == IST_COMPLETED || tr->state == IST_CONFIRMED || tr->state == IST_TERMINATED) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: transaction already answered\n"));
    return OSIP_WRONG_STATE;
  }

  if (jd == NULL)
    i = _eXosip_build_response_default (excontext, answer, NULL, code, tr->orig_request);
  else
    i = _eXosip_build_response_default (excontext, answer, jd->d_dialog, code, tr->orig_request);

  if (i != 0) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "ERROR: Could not create response for invite\n"));
    *answer = NULL;
    return i;
  }

  /* request that estabish a dialog: */
  /* 12.1.1 UAS Behavior */
  if (code > 100 && code < 300) {
    i = _eXosip_complete_answer_that_establish_a_dialog (excontext, *answer, tr->orig_request);
    if (i != 0) {
      osip_message_free (*answer);
      *answer = NULL;
      return i;
    }
  }


  if (send == 1) {
    osip_event_t *evt_answer;

    if (code >= 200 && code < 300 && jd != NULL) {
      _eXosip_dialog_set_200ok (jd, *answer);
      /* wait for a ACK */
      osip_dialog_set_state (jd->d_dialog, DIALOG_CONFIRMED);
    }

    evt_answer = osip_new_outgoing_sipmessage (*answer);
    evt_answer->transactionid = tr->transactionid;

    osip_transaction_add_event (tr, evt_answer);
    _eXosip_wakeup (excontext);
    *answer = NULL;
  }

  return OSIP_SUCCESS;
}

#ifndef MINISIZE

int
_eXosip_insubscription_answer_1xx (struct eXosip_t *excontext, eXosip_notify_t * jn, eXosip_dialog_t * jd, int code)
{
  osip_event_t *evt_answer;
  osip_message_t *response;
  int i;
  osip_transaction_t *tr;

  tr = _eXosip_find_last_inc_subscribe (jn, jd);
  if (tr == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: cannot find transaction to answer"));
    return OSIP_NOTFOUND;
  }

  if (jd == NULL)
    i = _eXosip_build_response_default (excontext, &response, NULL, code, tr->orig_request);
  else
    i = _eXosip_build_response_default (excontext, &response, jd->d_dialog, code, tr->orig_request);

  if (i != 0) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "ERROR: Could not create response for subscribe\n"));
    return i;
  }

  if (code > 100) {
    /* request that estabish a dialog: */
    /* 12.1.1 UAS Behavior */
    i = _eXosip_complete_answer_that_establish_a_dialog (excontext, response, tr->orig_request);
    if (i != 0) {
    }

    if (jd == NULL) {
      i = _eXosip_dialog_init_as_uas (&jd, tr->orig_request, response);
      if (i != 0) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: cannot create dialog!\n"));
      }
      else
        ADD_ELEMENT (jn->n_dialogs, jd);
    }
  }

  evt_answer = osip_new_outgoing_sipmessage (response);
  evt_answer->transactionid = tr->transactionid;

  osip_transaction_add_event (tr, evt_answer);
  _eXosip_wakeup (excontext);
  return i;
}

int
_eXosip_insubscription_answer_3456xx (struct eXosip_t *excontext, eXosip_notify_t * jn, eXosip_dialog_t * jd, int code)
{
  osip_event_t *evt_answer;
  osip_message_t *response;
  int i;
  osip_transaction_t *tr;

  tr = _eXosip_find_last_inc_subscribe (jn, jd);
  if (tr == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: cannot find transaction to answer"));
    return OSIP_NOTFOUND;
  }
  if (jd == NULL)
    i = _eXosip_build_response_default (excontext, &response, NULL, code, tr->orig_request);
  else
    i = _eXosip_build_response_default (excontext, &response, jd->d_dialog, code, tr->orig_request);
  if (i != 0) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "ERROR: Could not create response for subscribe\n"));
    return i;
  }

  if ((300 <= code) && (code <= 399)) {
    /* Should add contact fields */
    /* ... */
  }

  evt_answer = osip_new_outgoing_sipmessage (response);
  evt_answer->transactionid = tr->transactionid;

  osip_transaction_add_event (tr, evt_answer);
  _eXosip_wakeup (excontext);
  return OSIP_SUCCESS;
}

#endif
