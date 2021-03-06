/**************************************************************
 * 
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 * 
 *************************************************************/


package ifc.i18n;

import com.sun.star.i18n.TransliterationModules;
import com.sun.star.i18n.XExtendedTransliteration;
import com.sun.star.lang.Locale;
import lib.MultiMethodTest;

/**
 *
 */
public class _XExtendedTransliteration extends MultiMethodTest {
    public XExtendedTransliteration oObj = null;
//    private Locale loc = new Locale("ja", "JP", "") ;
    private Locale loc = new Locale("en", "US", "") ;
    
    public void before() {
        oObj.loadModule(TransliterationModules.LOWERCASE_UPPERCASE, loc);
    }
    
    /**
     * Check lowercase - uppercase conversion of chars
     */
    public void _transliterateChar2Char() {
        boolean result = true;
        char in = 'a';
        char out = ' ';
        try {
            out = oObj.transliterateChar2Char(in) ;
            result &= out == 'A';
            in = '$'; // should not be changed
            out = oObj.transliterateChar2Char(in) ;
            result &= out == '$';
        }
        catch(com.sun.star.i18n.MultipleCharsOutputException e) {
            e.printStackTrace((java.io.PrintWriter)log);
        }
        tRes.tested("transliterateChar2Char()", result);
    }
    
    /**
     * Check lowercase - uppercase conversion of char to string
     */
    public void _transliterateChar2String() {
        boolean result = true;
        char in = 'a';
        String out = null;
        out = oObj.transliterateChar2String('a') ;
        result &= out.equals("A");
        in = '$'; // should not be changed
        out = oObj.transliterateChar2String(in) ;
        result &= out.equals("$");
        tRes.tested("transliterateChar2String()", result);
    }
    
    /**
     * Check lowercase - uppercase conversion of strings
     */
    public void _transliterateString2String() {
        boolean result = true;
        String in = "aAbBcC";
        String out = null;
        out = oObj.transliterateString2String(in, 0, 6) ;
        result &= out.equals("AABBCC");
        in = "$"; // should not be changed
        out = oObj.transliterateString2String(in, 0, 1) ;
        result &= out.equals("$");
        tRes.tested("transliterateString2String()", result);
    }
}
