<?php

/**
 * @file
 * Utility functions.
 */

/**
 * Get the DOI name for the given url.
 *
 * @param string $doi_url
 *   A DOI url, or a DOI name with or without the "doi:" prefix. If the $url
 *   is a CrossRef ID passed in as doi:10.1016/j.tiv.2011.10.017 or
 *   10.1111/eve.12339 it will not get changed by running it through parse_url.
 *
 * @return string
 *   A string containing the DOI name.
 */
function islandora_doi_get_doi_name_from_url($doi_url) {
  // Allows for DOI to be entered in the following formats: 10.1111/eva.12339,
  // http://dx.doi.org/10.1111/eva.12339, http://doi.org/10.1111/eva.12339,
  // or doi:10.1111/eva.12339. Removing whitespace before parsing the url and
  // then removing any trailing "/" from the returned path.
  $doi_name = trim(parse_url(trim($doi_url), PHP_URL_PATH), '/');
  return $doi_name;
}

/**
 * Perform a request to CrossRef for the given ID.
 *
 * @param string $id_or_url
 *   A DOI to lookop. May be provided with or without the "doi:" prefix. Can
 *   also pass a full DOI url (i.e, http://doi.org/10.1111/eva.12339 ).
 *
 * @return object
 *   An object as provided by drupal_http_request().
 */
function islandora_doi_perform_request($id_or_url) {
  // Allows for $id to pass a DOI url string or the DOI name.
  $id = islandora_doi_get_doi_name_from_url($id_or_url);
  $openurl = variable_get('islandora_doi_openurl', 'http://www.crossref.org/openurl');
  $openurl_pid = variable_get('islandora_doi_openurl_pid', 'user@example.com');
  $url = url($openurl, array(
           'query' => array(
             'noredirect' => 'true',
             'pid' => $openurl_pid,
             'format' => 'unixref',
             'id' => ((strpos($id, 'doi:') === 0) ? $id : 'doi:' . $id),
           ),
  ));

  return drupal_http_request($url);
}

/**
 * Produce MODS from the given CrossRef XML.
 *
 * @param mixed $doi_xml
 *   XML containing the parsed response from CrossRef. Should be passed as a 
 *   DOMDocument, but also works with a SimpleXMLElement.
 *
 * @return DOMDocument|bool
 *   A DOMDocument containing the constructed MODS, or FALSE on failure.
 *
 * @throws Exception
 *   If passed a SimpleXMLElement that can't be converted to DOMDocument.
 *
 * @throws InvalidArgumentException
 *   If passed something other than DOMDocument or SimpleXMLElement.
 */
function islandora_doi_transform_to_mods($doi_xml) {
  // Make sure doi_xml is a DOMDocument. Otherwise show a warning and, if it
  // is a SimpleXMLElement, convert it.
  if ($doi_xml instanceof SimpleXMLElement) {
    // Warning message: Converting from SimpleXMLElement.
    watchdog('Islandora DOI', 'islandora_doi_transform_to_mods should be passed a DOMDocument but received a SimpleXMLElement. Attempting to convert the SimpleXMLElement to a DOMDocument. Code should be updated to call islandora_doi_transform_to_mods with a DOMDocument instead.');
    trigger_error(t('islandora_doi_transform_to_mods should be passed a DOMDocument but received a SimpleXMLElement. Attempting to convert the SimpleXMLElement to a DOMDocument. Code should be updated to call islandora_doi_transform_to_mods with a DOMDocument instead.'));
    $dom_xml = new DOMDocument();
    if (!$dom_xml = dom_import_simplexml($doi_xml)) {
      // Error message: Failed to convert from SimpleXMLElement.
      throw new Exception('Failed to convert SimpleXMLElement to DOMDocument');
    }
    $doi_xml = $dom_xml;
  }
  elseif (!($doi_xml instanceof DOMDocument)) {
    // Error message: Not supplied with a DOMDocument.
    throw new InvalidArgumentException('islandora_doi_transform_to_mods should take a DOMDocument');
  }

  // Create MODS XML.
  $mods = new DOMDocument('1.0');
  $mods->loadXML('<mods xmlns="http://www.loc.gov/mods/v3" xmlns:mods="http://www.loc.gov/mods/v3" xmlns:xlink="http://www.w3.org/1999/xlink"/>');

  // @todo Implement book support.

  // Add metadata for journal articles.
  foreach ($doi_xml->getElementsByTagName('journal') as $journal) {
    $genre = $mods->createElement('genre');
    $genre->nodeValue = 'article';
    $mods->firstChild->appendChild($genre);

    $relateditem = $mods->createElement('relatedItem');
    $mods->firstChild->appendChild($relateditem);
    $relateditem_attribute = $mods->createAttribute('type');
    $relateditem_attribute->value = 'host';
    $relateditem->appendChild($relateditem_attribute);

    foreach ($journal->getElementsByTagName('journal_metadata') as $metadata) {
      foreach ($metadata->childNodes as $child) {
        switch ($child->nodeName) {
          case 'full_title':
            $titleinfo = $mods->createElement('titleInfo');
            $relateditem->appendChild($titleinfo);
            $title = $mods->createElement('title');
            $titleinfo->appendChild($title);
            if (!is_null($child->firstChild)) {
              $title_text = $mods->importNode($child->firstChild);
              $title->appendChild($title_text);
            }
            break;

          case 'abbrev_title':
            $titleinfo = $mods->createElement('titleInfo');
            $titleinfo_attribute = $mods->createAttribute('type');
            $titleinfo_attribute->value = 'abbreviated';
            $titleinfo->appendChild($titleinfo_attribute);
            $relateditem->appendChild($titleinfo);
            $title = $mods->createElement('title');
            $titleinfo->appendChild($title);
            if (!is_null($child->firstChild)) {
              $title_text = $mods->importNode($child->firstChild);
              $title->appendChild($title_text);
            }
            break;

          case 'issn':
            $identifier = $mods->createElement('identifier');
            $identifier_attribute = $mods->createAttribute('type');
            $identifier_attribute->value = 'issn';
            $identifier->appendChild($identifier_attribute);
            $relateditem->appendChild($identifier);
            if (!is_null($child->firstChild)) {
              $identifier_text = $mods->importNode($child->firstChild);
              $identifier->appendChild($identifier_text);
            }
            break;
        }
        foreach ($journal->journal_issue as $issue) {
          foreach ($issue->children() as $name => $item) {
            switch ((string) $name) {
              case 'contributors':
                break;

              case 'publication_date':
                $pubdate_year = (string) $item[0]->year[0];
                $pubdate_month = (string) $item[0]->month[0];
                $pubdate_day = (string) $item[0]->day[0];
                break;

              case 'journal_volume':
                $volume_number = (string) $item[0]->volume[0];
                break;

              case 'issue':
                $issue_number = (string) $item[0];
                break;

              case 'special_numbering':
                break;

              case 'doi_data':
                break;

            }
          }
        }
        foreach ($journal->journal_article as $article) {
          foreach ($article->attributes() as $name => $item) {
            switch ((string) $name) {
              case 'publication_type':
                // Options: full_text, abstract_only, bibliographic_record
                // doesn't seem to matter here.
                break;

              case 'language':
                $language = $mods->addChild('language');
                $languageterm = $language->addChild('languageTerm', (string) $item[0]);
                if (strlen((string) $item[0]) == 2) {
                  $languageterm->addAttribute('type', 'code');
                  $languageterm->addAttribute('authority', 'rfc4646');
                }
                break;

            }
          }
          $nodes = $article->xpath("publication_date[@media_type='print']");
          if ($nodes) {
              $pubdate_year = (string) $nodes[0]->year[0];
              $pubdate_month = (string) $nodes[0]->month[0];
              $pubdate_day = (string) $nodes[0]->day[0];
              
          } else {
            $nodes = $article->xpath("publication_date[@media_type='online']");
            if ($nodes) {
              $pubdate_year = (string) $nodes[0]->year[0];
              $pubdate_month = (string) $nodes[0]->month[0];
              $pubdate_day = (string) $nodes[0]->day[0];
            } 
          }
          
          foreach ($article->children() as $name => $item) {
            switch ((string) $name) {
              case 'titles':
                $title_info = $mods->addChild('titleInfo');
                $title_info->addChild('title', (string) $item->title[0]);
//                if ($item->subtitle) {
//                  $title_info->addChild('subTitle', (string) $item->subtitle[0]);
//                }
                if ($item->original_language_title) {
                  // XXX: doesn't seem to actually add the original language
                  // title... creates the element, with attributes, but no
                  // content?
                  $title_info_translated = $mods->addChild('titleInfo');
                  $title_info_translated->addAttribute('type', 'translated');
                  $title_info_translated->addAttribute('lang', (string) $item->original_language_title['language']);
                }
                break;

              case 'contributors':
                foreach ($item->children() as $type => $contributor) {
                  switch ((string) $type) {
                    case 'person_name':
                      $role = strtolower((string) $contributor['contributor_role']);
                      $sequence = (string) $contributor['sequence'];
                      $given_name = (string) $contributor->given_name;
                      $given_name2 = explode(" ", (string) $contributor->given_name, 2);
                      $surname = (string) $contributor->surname;
                      $suffix = (string) $contributor->suffix;
                      $affiliation = (string) $contributor->affiliation;
                      $orchid = str_replace('http://orcid.org/', '', (string) $contributor->ORCID);

                      $mods_name = $mods->addChild('name');
                      $mods_name->addAttribute('type', 'personal');

                      $given_name_part = $mods_name->addChild('namePart', $given_name);
                      $given_name_part->addAttribute('type', 'given');
                      $surname_part = $mods_name->addChild('namePart', $surname);
                      $surname_part->addAttribute('type', 'family');

                      if ($sequence == 'first') {
                        $mods_name->addAttribute('usage', 'primary');
                      }

                      if ($role) {
                        $name_role = $mods_name->addChild('role');
                        $name_role->addChild('roleTerm', $role);
                        $roles = array('author', 'editor', 'translator');
                        if (in_array($role, $roles)) {
                          $name_role->roleTerm->addAttribute('authority', 'marcrelator');
                          $name_role->roleTerm->addAttribute('type', 'text');
                        }
                      }

                      if ($affiliation) {
                        $mods_name->addChild('affiliation', $affiliation);
                      }

                      if ($suffix) {
                        $terms_of_address = $mods_name->addChild('namePart', $suffix);
                        $terms_of_address->addAttribute('type', 'termsOfAddress');
                      }
                      
                      if ($orchid) {
                          $orchid1 = $mods_name->addChild('nameIdentifier', $orchid);
                          $orchid1->addAttribute('type', 'ORCID');
                      }
                      break;

                    case 'organization':
                      $role = strtolower((string) $contributor['contributor_role']);
                      $sequence = (string) $contributor['sequence'];
                      $mods_name = $mods->addChild('name');
                      $mods_name->addAttribute('type', 'corporate');
                      $mods_name->addChild('namePart', (string) $contributor);
                      break;

                  }
                }
                break;

              // Genre element was not getting populated and it is needed
              // for proper CSL display. If we start pulling anything other
              // than journal articles using DOIs, then this will need to
              // be revisited. Journal article will work when piped through
              // CSL converter.
              case 'genre':
                $genre = 'journal article';
                $mods_genre = $mods->addChild('genre', $genre);
                break;

              case 'publication_date':
//                $pubdate_year = (string) $item[0]->year[0];
//                $pubdate_month = (string) $item[0]->month[0];
//                $pubdate_day = (string) $item[0]->day[0];
                break;

              case 'pages':
                $first_page = (string) $item[0]->first_page[0];
                $last_page = (string) $item[0]->last_page[0];
                $other_pages = (string) $item[0]->other_pages[0];
                break;

              case 'publisher_item':
                // @todo.
                  $article_number = (string) $item->item_number;
                break;

              case 'crossmark':
                // @todo.
                break;

              case 'doi_data':
                foreach ($item->doi as $doi) {
                  $mods_identifier = $mods->addChild('identifier', (string) $doi);
                  $mods_identifier->addAttribute('type', 'doi');
                  $mods_relatedItem2 = $mods->addChild('relatedItem');
                  $mods_relatedItem2->addAttribute('type', 'isReferencedBy');
                  $mods_identifier2 = $mods_relatedItem2->addChild('identifier');
                  $mods_identifier2->addAttribute('type', 'uri');
                  $mods_identifier2->{0} = 'http://dx.doi.org/' . (string) $doi;
                  $abstract = islandora_doi_get_abstract((string) $doi);
                  if ($abstract) {
                      $mods_abstract = $mods->addchild('abstract', (string) $abstract);
                  }
                }
                foreach ($item->resource as $resource) {
//                  $mods_identifier = $mods->addChild('identifier');
//                  $mods_identifier->{0} = (string) $resource;
//                  $mods_identifier->addAttribute('type', 'uri');
                }
                break;

              case 'citation_list':
                // @todo.
                break;

              case 'component_list':
                // @todo.
                break;
            }
          }
        }

        // We should populated the dateIssued element otherwise CSL does
        // not display a date.
        $origin_info = $mods->addChild('originInfo');
        if (!empty($pubdate_year)) {
          $date_value = $pubdate_year;
          if (!empty($pubdate_month)) {
            if (strlen($pubdate_month) == 1) {
              $pubdate_month = "0" . $pubdate_month;
            }
            $date_value .= "-" . $pubdate_month;
          }
          if (!empty($pubdate_day)) {
            if (strlen($pubdate_day) == 1) {
              $pubdate_day = "0" . $pubdate_day;
            }
            $date_value .= "-" . $pubdate_day;
          }
          $dateIssued = $origin_info->addChild('dateIssued', $date_value);
          $dateIssued->addAttribute('encoding', 'w3cdtf');
          $dateIssued->addAttribute('keyDate', 'yes');
        }

        if (!empty($volume_number) || !empty($issue_number) || !empty($other_pages) || !empty($last_page) || !empty($first_page) || !empty($pubdate_year) || !empty($pubdate_month) || !empty($pubdate_day)) {
          $part = $relateditem->addChild('part');

          if (!empty($last_page) || !empty($first_page) || !empty($other_pages)) {
            $extent = $part->addChild('extent');
            $extent->addAttribute('unit', 'page');
//            if (!empty($other_pages)) {
//              $extent->addChild('list', $other_pages);
//            }
            if (!empty($last_page) || !empty($first_page)) {
              $extent->addChild('start', $first_page);
              $extent->addChild('end', $last_page);
            }
            if (!empty($last_page) && !empty($first_page)) {
              $extent->addChild('list', ($first_page . '-' . $last_page));
            }
          }

          if (!empty($volume_number)) {
            $volume = $part->addChild('detail');
            $volume->addAttribute('type', 'volume');
            $volume->addChild('number', $volume_number);
          }
          if (!empty($issue_number)) {
            $issue = $part->addChild('detail');
            $issue->addAttribute('type', 'issue');
            $issue->addChild('number', $issue_number);
          }
          if (!empty($article_number)) {
            $article_num = $part->addChild('detail');
            $article_num->addAttribute('type', 'citation/article');
            $article_num->addChild('number', $article_number);
          }
          if (!empty($pubdate_year)) {
            $date_value = $pubdate_year;
            if (!empty($pubdate_month)) {
              if (strlen($pubdate_month) == 1) {
                $pubdate_month = "0" . $pubdate_month;
              }
              $date_value .= "-" . $pubdate_month;
            }
            if (!empty($pubdate_day)) {
              if (strlen($pubdate_day) == 1) {
                $pubdate_day = "0" . $pubdate_day;
              }
              $date_value .= "-" . $pubdate_day;
            }
//  Removed following statement so data wasn't in part section.            
//            $part->addChild('date', $date_value);
          }

        }

        // Return after first instance.
        return $mods;
      }
    }
    foreach ($journal->getElementsByTagName('journal_issue') as $issue) {
      if ($issue->hasChildNodes()) {
        foreach ($issue->childNodes as $child) {
          switch ($child->nodeName) {
            case 'publication_date':
              if ($child->hasChildNodes()) {
                foreach ($child->childNodes as $date_part) {
                  switch ($date_part->nodeName) {
                    case 'month':
                      $pubdate_month = $date_part->nodeValue;
                      break;

                    case 'year':
                      $pubdate_year = $date_part->nodeValue;
                      break;

                    case 'day':
                      $pubdate_day = $date_part->nodeValue;
                      break;
                  }
                }
              }
              break;

            case 'journal_volume':
              if ($child->hasChildNodes()) {
                foreach ($child->childNodes as $volume) {
                  switch ($volume->nodeName) {
                    case 'volume':
                      if (!is_null($volume->firstChild)) {
                        $volume_number = $mods->importNode($volume->firstChild);
                      }
                      break;
                  }
                }
              }
              break;

            case 'issue':
              if (!is_null($child->firstChild)) {
                $issue_number = $mods->importNode($child->firstChild);
              }
              break;
          }
        }
      }
    }
    foreach ($journal->getElementsByTagName('journal_article') as $article) {
      if ($article->hasAttributes()) {
        foreach ($article->attributes as $att) {
          switch ($att->name) {
            case 'language':
              $language = $mods->createElement('language');
              $mods->firstChild->appendChild($language);
              $languageterm = $mods->createElement('languageTerm');
              $language->appendChild($languageterm);
              $language_text = $mods->createTextNode($att->value);
              $languageterm->appendChild($language_text);
              if (strlen($att->value) == 2) {
                $type_attribute = $mods->createAttribute('type');
                $type_attribute->value = 'code';
                $languageterm->appendChild($type_attribute);
                $authority_attribute = $mods->createAttribute('authority');
                $authority_attribute->value = 'rfc4646';
                $languageterm->appendChild($authority_attribute);
              }
              break;
          }
        }
      }
      if ($article->hasChildNodes()) {
        foreach ($article->childNodes as $child) {
          switch ($child->nodeName) {
            case 'titles':
              $titleinfo = $mods->createElement('titleInfo');
              $mods->firstChild->appendChild($titleinfo);
              foreach ($child->childNodes as $grandchild) {
                switch ($grandchild->nodeName) {
                  case 'title':
                    $article_title = $mods->importNode($grandchild, TRUE);
                    $titleinfo->appendChild($article_title);
                    break;

                  case 'subtitle':
                    $article_subtitle = $mods->createElement('subTitle');
                    if (!is_null($grandchild->firstChild)) {
                      $article_subtitle_text = $mods->importNode($grandchild->firstChild, TRUE);
                      $article_subtitle->appendChild($article_subtitle_text);
                    }
                    $titleinfo->appendChild($article_subtitle);
                    break;

                  case 'original_language_title':
                    $translated_titleinfo = $mods->createElement('titleInfo');
                    $mods->firstChild->appendChild($translated_titleinfo);
                    $titleinfo_attribute = $mods->createAttribute('type');
                    $titleinfo_attribute->value = 'translated';
                    $translated_titleinfo->appendChild($titleinfo_attribute);
                    foreach ($grandchild->attributes as $att) {
                      switch ($att->nodeName) {
                        case 'language':
                          $titleinfo_attribute = $mods->createAttribute('lang');
                          $titleinfo_attribute->value = $att->nodeValue;
                          $translated_titleinfo->appendChild($titleinfo_attribute);
                          break;
                      }
                    }

                    $translated_title = $mods->createElement('title');
                    $translated_titleinfo->appendChild($translated_title);
                    if (!is_null($grandchild->firstChild)) {
                      $translated_title_text = $mods->importNode($grandchild->firstChild);
                      $translated_title->appendChild($translated_title_text);
                    }
                    break;
                }
              }
              break;

            case 'contributors':
              foreach ($child->getElementsByTagName('organization') as $organization) {
                $name = $mods->createElement('name');
                $mods->firstChild->appendChild($name);
                $name_attribute = $mods->createAttribute('type');
                $name_attribute->value = 'corporate';
                $name->appendChild($name_attribute);
                $namepart = $mods->createElement('namePart');
                $name->appendChild($namepart);
                if (!is_null($organization->firstChild)) {
                  $namepart_text = $mods->importNode($organization->firstChild);
                  $namepart->appendChild($namepart_text);
                }
              }

              foreach ($child->getElementsByTagName('person_name') as $person) {
                $name = $mods->createElement('name');
                $mods->firstChild->appendChild($name);
                $name_attribute = $mods->createAttribute('type');
                $name_attribute->value = 'personal';
                $name->appendChild($name_attribute);

                foreach ($person->childNodes as $person_part) {
                  switch ($person_part->nodeName) {
                    case 'given_name':
                      $namepart = $mods->createElement('namePart');
                      $name->appendChild($namepart);
                      $namepart_attribute = $mods->createAttribute('type');
                      $namepart_attribute->value = 'given';
                      $namepart->appendChild($namepart_attribute);
                      if (!is_null($person_part->firstChild)) {
                        $given_name_part = $mods->importNode($person_part->firstChild);
                        $namepart->appendChild($given_name_part);
                      }
                      break;

                    case 'surname':
                      $namepart = $mods->createElement('namePart');
                      $name->appendChild($namepart);
                      $namepart_attribute = $mods->createAttribute('type');
                      $namepart_attribute->value = 'family';
                      $namepart->appendChild($namepart_attribute);
                      if (!is_null($person_part->firstChild)) {
                        $surname_part = $mods->importNode($person_part->firstChild);
                        $namepart->appendChild($surname_part);
                      }
                      break;

                    case 'suffix':
                      $terms_of_address = $mods->createElement('namePart');
                      $name->appendChild($terms_of_address);
                      if (!is_null($person_part->firstChild)) {
                        $terms_of_address_text = $mods->importNode($person_part->firstChild);
                        $terms_of_address->appendChild($terms_of_address_text);
                      }
                      $terms_of_address_attribute = $mods->createAttribute('type');
                      $terms_of_address_attribute->value = 'termsOfAddress';
                      $terms_of_address->appendChild($terms_of_address_attribute);
                      break;

                    case 'affiliation':
                      $affiliation = $mods->createElement('affiliation');
                      $name->appendChild($affiliation);
                      if (!is_null($person_part->firstChild)) {
                        $affiliation_text = $mods->importNode($person_part->firstChild);
                        $affiliation->appendChild($affiliation_text);
                      }
                      break;
                  }
                }

                foreach ($person->attributes as $att) {
                  switch ($att->nodeName) {
                    case 'contributor_role':
                      $role = $mods->createElement('role');
                      $name->appendChild($role);
                      $roleterm = $mods->createElement('roleTerm');
                      $role->appendChild($roleterm);
                      $roleterm_text = $mods->createTextNode(strtolower($att->nodeValue));
                      $roleterm->appendChild($roleterm_text);

                      $roles = array('author', 'editor', 'translator');
                      if (in_array(strtolower($att->nodeValue), $roles)) {
                        $roleterm_attribute = $mods->createAttribute('authority');
                        $roleterm_attribute->value = 'marcrelator';
                        $roleterm->appendChild($roleterm_attribute);
                        $roleterm_attribute = $mods->createAttribute('type');
                        $roleterm_attribute->value = 'text';
                        $roleterm->appendChild($roleterm_attribute);
                      }
                      break;

                    case 'sequence':
                      if (strcmp('first', strtolower($att->nodeValue)) == 0) {
                        $name_attribute = $mods->createAttribute('usage');
                        $name_attribute->value = 'primary';
                        $name->appendChild($name_attribute);
                      }
                  }
                }
              }
              break;

            case 'publication_date':
              // Note that this overwrites the publication date that came
              // from the journal metadata above.
              if ($child->hasChildNodes()) {
                foreach ($child->childNodes as $date_part) {
                  switch ($date_part->nodeName) {
                    case 'month':
                      $pubdate_month = $date_part->nodeValue;
                      break;

                    case 'year':
                      $pubdate_year = $date_part->nodeValue;
                      break;

                    case 'day':
                      $pubdate_day = $date_part->nodeValue;
                      break;
                  }
                }
              }
              break;

            case 'pages':
              if ($child->hasChildNodes()) {
                foreach ($child->childNodes as $page_part) {
                  switch ($page_part->nodeName) {
                    case 'first_page':
                      if (!is_null($page_part->firstChild)) {
                        $first_page = $mods->importNode($page_part->firstChild);
                      }
                      break;

                    case 'last_page':
                      if (!is_null($page_part->firstChild)) {
                        $last_page = $mods->importNode($page_part->firstChild);
                      }
                      break;

                    case 'other_pages':
                      if (!is_null($page_part->firstChild)) {
                        $other_pages = $mods->importNode($page_part->firstChild);
                      }
                      break;
                  }
                }
              }
              break;

            case 'publisher_item':
              // @todo
              break;

            case 'crossmark':
              // @todo
              break;

            case 'doi_data':
              if ($child->hasChildNodes()) {
                foreach ($child->childNodes as $identifier) {
                  switch ($identifier->nodeName) {
                    case 'doi':
                      if (!is_null($identifier->firstChild)) {
                        $doi = $mods->createElement('identifier');
                        $mods->firstChild->appendChild($doi);
                        $doi_attribute = $mods->createAttribute('type');
                        $doi_attribute->value = 'doi';
                        $doi->appendChild($doi_attribute);
                        $doi_text = $mods->importNode($identifier->firstChild);
                        $doi->appendChild($doi_text);
                      }
                      break;

                    case 'resource':
                      if (!is_null($identifier->firstChild)) {
                        $uri = $mods->createElement('identifier');
                        $mods->firstChild->appendChild($uri);
                        $uri_attribute = $mods->createAttribute('type');
                        $uri_attribute->value = 'uri';
                        $uri->appendChild($uri_attribute);
                        $uri_text = $mods->importNode($identifier->firstChild);
                        $uri->appendChild($uri_text);
                      }
                      break;
                  }
                }
              }
              break;

            case 'citation_list':
              // @todo
              break;

            case 'component_list':
              // @todo
              break;
          }
        }
      }
    }
    // Add the publication date if it was read in one of the two places above.
    $origininfo = $mods->createElement('originInfo');
    $mods->firstChild->appendChild($origininfo);

    if (!empty($pubdate_year)) {
      $date_value = $pubdate_year;
      if (!empty($pubdate_month)) {
        if (strlen($pubdate_month) == 1) {
          $pubdate_month = "0" . $pubdate_month;
        }
        $date_value .= "-" . $pubdate_month;
      }
      if (!empty($pubdate_day)) {
        if (strlen($pubdate_day) == 1) {
          $pubdate_day = "0" . $pubdate_day;
        }
        $date_value .= "-" . $pubdate_day;
      }
      $dateissued = $mods->createElement('dateIssued');
      $origininfo->appendChild($dateissued);
      $dateissued->nodeValue = $date_value;
    }

    // Add the page, volume, and issue data if it was read above.
    if (!empty($volume_number) || !empty($issue_number) || !empty($other_pages) || !empty($last_page) || !empty($first_page) || !empty($pubdate_year) || !empty($pubdate_month) || !empty($pubdate_day)) {
      $part = $mods->createElement('part');
      $mods->firstChild->appendChild($part);

      if (!empty($last_page) || !empty($first_page) || !empty($other_pages)) {
        $extent = $mods->createElement('extent');
        $part->appendChild($extent);
        $extent_attribute = $mods->createAttribute('unit');
        $extent_attribute->value = 'page';
        $extent->appendChild($extent_attribute);

        if (!empty($other_pages)) {
          $list = $mods->createElement('list');
          $list->appendChild($other_pages);
          $extent->appendChild($list);
        }
        if (!empty($first_page)) {
          $start = $mods->createElement('start');
          $start->appendChild($first_page);
          $extent->appendChild($start);
        }
        if (!empty($last_page)) {
          $end = $mods->createElement('end');
          $end->appendChild($last_page);
          $extent->appendChild($end);
        }
      }

      if (!empty($volume_number)) {
        $volume = $mods->createElement('detail');
        $part->appendChild($volume);
        $volume_attribute = $mods->createAttribute('type');
        $volume_attribute->value = 'volume';
        $volume->appendChild($volume_attribute);
        $number = $mods->createElement('number');
        $volume->appendChild($number);
        $number->appendChild($volume_number);
      }
      if (!empty($issue_number)) {
        $issue = $mods->createElement('detail');
        $part->appendChild($issue);
        $issue_attribute = $mods->createAttribute('type');
        $issue_attribute->value = 'issue';
        $issue->appendChild($issue_attribute);
        $number = $mods->createElement('number');
        $issue->appendChild($number);
        $number->appendChild($issue_number);
      }

      if (!empty($date_value)) {
        $date = $mods->createElement('date');
        $date->nodeValue = $date_value;
        $part->appendChild($date);
      }
    }
    // Return after first instance.
    return $mods;
  }

  return FALSE;
}
/**
 *  * Get abstract form WOS for the given DOI.
 *
 * @param string $id
 *   A DOI.
 *
 * @return String 
 *   A string containing a abstract.
 */
 
function islandora_doi_get_abstract($id) {
    $auth_url = "http://search.webofknowledge.com/esti/wokmws/ws/WOKMWSAuthenticate?wsdl";

    $auth_client = @new SoapClient($auth_url, array('login' => "NCAR",
        'password' => "OLCP2YXI"));
//$auth_client = @new SoapClient($auth_url);
    $auth_response = $auth_client->authenticate();
    $search_url = "http://search.webofknowledge.com/esti/wokmws/ws/WokSearch?wsdl";
    $search_client = @new SoapClient($search_url);
    $search_client->__setCookie('SID', $auth_response->return);

    $search_array = array(
        'queryParameters' => array(
            'databaseId' => 'WOS',
            'userQuery' => 'DO=' . $id,
            'editions' => array(
                array('collection' => 'WOS', 'edition' => 'SCI')
#      array('collection' => 'WOS', 'edition' => 'SCI')
            ),
            'queryLanguage' => 'en'
        ),
        'retrieveParameters' => array(
            'count' => '5',
            'fields' => array(
                array('name' => 'Date', 'sort' => 'D')
            ),
            'firstRecord' => '1',
            'viewField' => array('collectionName' => 'WOS', 'fieldName' => 'abstract')
        )
    );

    try {
        $search_response = $search_client->search($search_array);
        if ($search_response->return->recordsFound > 0) {
            $returnedxml = simplexml_load_string($search_response->return->records);
            $abstractsxml = $returnedxml->REC->static_data->fullrecord_metadata->abstracts->abstract->abstract_text;
            $abstract = (string) $abstractsxml->p;
        } else {
            $abstract = NULL;
        }
    } catch (Exception $e) {
        echo $e->getMessage();
        $abstract = "Error";
    }
    return $abstract;
}

/**
 * Get MODS for the given DOI.
 *
 * @param string $id
 *   A DOI.
 *
 * @return SimpleXMLElement|bool
 *   A SimpleXMLElement containing a MODS document, or FALSE if either an error
 *   occured or nothing could be transformed.
 */
function islandora_doi_get_mods($id) {
  $response = islandora_doi_perform_request($id);
  $headers = array_change_key_case($response->headers);
  if ($response->data &&
    strpos($headers['content-type'], 'text/html') === FALSE &&
    strpos($response->data, "Malformed DOI") === FALSE) {
    $crossref_xml = new DOMDocument('1.0');
    if (!$crossref_xml->loadXML($response->data)) {
      return FALSE;
    }
    else {
      return islandora_doi_transform_to_mods($crossref_xml);
    }
  }
  else {
    return FALSE;
  }
}
