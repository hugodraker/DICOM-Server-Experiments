Option Explicit

Dim fso, outFile, i, idNum, firstName, lastName, birthDate, sex
Dim phoneNumber, altPhone, email, altContactName, altContactNumber
Dim streetAddress, city, state, zipCode
Dim firstNames, lastNames, rngMonth, rngDay, rngYear
Dim minYear, maxYear

' --- Male first names ---
Dim maleNames
maleNames = Array( _
    "James","Robert","John","Michael","William","David","Richard","Joseph", _
    "Thomas","Charles","Christopher","Daniel","Matthew","Anthony","Mark", _
    "Donald","Steven","Paul","Andrew","Joshua","Kenneth","Kevin","Brian", _
    "George","Ronald","Timothy","Jason","Jeffrey","Ryan","Jacob","Gary", _
    "Nicholas","Eric","Jonathan","Stephen","Larry","Justin","Scott","Brandon", _
    "Benjamin","Samuel","Gregory","Frank","Alexander","Raymond","Patrick", _
    "Jack","Dennis","Jerry","Tyler","Aaron" )

' --- Female first names ---
Dim femaleNames
femaleNames = Array( _
    "Mary","Patricia","Jennifer","Linda","Elizabeth","Barbara","Susan","Jessica", _
    "Sarah","Karen","Nancy","Lisa","Margaret","Betty","Sandra","Ashley", _
    "Kimberly","Emily","Donna","Michelle","Carol","Amanda","Dorothy","Melissa", _
    "Deborah","Stephanie","Rebecca","Laura","Sharon","Cynthia","Kathleen","Amy", _
    "Angela","Shirley","Anna","Brenda","Pamela","Emma","Nicole","Samantha", _
    "Katherine","Christine","Debra","Rachel","Catherine","Carolyn","Janet", _
    "Ruth","Maria","Heather","Diane","Julie" )

' --- Last names pool ---
Dim lastNamesArray
lastNamesArray = Array( _
    "Smith","Johnson","Williams","Brown","Jones","Garcia","Miller","Davis", _
    "Rodriguez","Martinez","Hernandez","Lopez","Gonzalez","Wilson","Anderson", _
    "Thomas","Taylor","Moore","Jackson","Martin","Lee","Perez","Thompson", _
    "White","Harris","Sanchez","Clark","Ramirez","Lewis","Robinson","Walker", _
    "Young","Allen","King","Wright","Scott","Torres","Nguyen","Hill", _
    "Flores","Green","Adams","Nelson","Baker","Hall","Rivera","Campbell", _
    "Mitchell","Carter","Roberts","Gomez","Phillips","Evans","Turner","Diaz", _
    "Parker","Cruz","Edwards","Collins","Reyes","Stewart","Morris","Morales", _
    "Murphy","Cook","Rogers","Gutierrez","Ortiz","Morgan","Cooper","Peterson", _
    "Bailey","Reed","Kelly","Howard","Ramos","Kim","Cox","Ward", _
    "Richardson","Watson","Brooks","Chavez","Wood","Bennett","Gray", _
    "Mendoza","Ruiz","Hughes","Price","Alvarez","Castillo","Sanders","Patel", _
    "Myers","Long","Ross","Foster","Jimenez","Powell","Jenkins","Perry" )

' --- Street types ---
Dim streetTypes
streetTypes = Array("Street","Avenue","Road","Drive","Lane","Boulevard","Court","Way","Plaza","Circle")

' --- Cities and States (paired)
Dim cities, states
cities = Array( _
    "New York","Los Angeles","Chicago","Houston","Phoenix","Philadelphia", _
    "San Antonio","San Diego","Dallas","San Jose","Austin","Jacksonville", _
    "Fort Worth","Columbus","Charlotte","Indianapolis","Seattle","Denver", _
    "Washington","Boston","El Paso","Nashville","Detroit","Oklahoma City", _
    "Portland","Las Vegas","Memphis","Louisville","Baltimore","Milwaukee", _
    "Albuquerque","Tucson","Fresno","Sacramento","Kansas City","Atlanta", _
    "Miami","Cleveland","Minneapolis","Tampa","New Orleans","Honolulu")

states = Array( _
    "NY","CA","IL","TX","AZ","PA","TX","CA","TX","CA","TX","FL", _
    "TX","OH","NC","IN","WA","CO","DC","MA","TX","TN","MI","OK", _
    "OR","NV","TN","KY","MD","WI","NM","AZ","CA","CA","MO","GA", _
    "FL","OH","MN","FL","LA","HI")

' --- ZIP code ranges by state (approximate) ---
Dim zipPrefixes
zipPrefixes = Array( _
    "100","900","606","770","850","191","782","921","752","951","733","321", _
    "761","430","280","462","981","802","200","021","798","372","482","731", _
    "972","891","381","402","212","531","871","857","937","958","641","303", _
    "331","441","551","336","701","968")

' --- Settings ---
minYear = 1925
maxYear = 2010
Const NUM_RECORDS = 10000

' --- Randomize ---
Randomize Timer

' --- Create file ---
Set fso = CreateObject("Scripting.FileSystemObject")
outFile = "patients.csv"
Set outFile = fso.CreateTextFile(outFile, True, False)

' --- Write header ---
outFile.WriteLine "ID,Name,Sex,Birthdate,Phone,AltPhone,Email,AltContactName,AltContactPhone,Address,City,State,ZIP"

' --- Generate records ---
For i = 1 To NUM_RECORDS
    idNum = Right("00000000" & CStr(i), 8)

    firstName = PickFirstName()
    lastName  = lastNamesArray(Int(Rnd() * UBound(lastNamesArray) + 1))

    rngYear  = minYear + Int(Rnd() * (maxYear - minYear + 1))
    rngMonth = 1 + Int(Rnd() * 12)
    rngDay   = 1 + Int(Rnd() * Day(DateSerial(rngYear, rngMonth + 1, 0)))

    birthDate = rngYear & Right("0" & rngMonth, 2) & Right("0" & rngDay, 2)
    
    ' Determine sex based on first name
    If InArray(firstName, maleNames) Then
        sex = "M"
    Else
        sex = "F"
    End If

    ' Generate contact info
    phoneNumber      = GeneratePhoneNumber()
    altPhone         = GeneratePhoneNumber()
    email            = GenerateEmail(firstName, lastName)
    altContactName   = GenerateAltContactName(sex)
    altContactNumber = GeneratePhoneNumber()
    streetAddress    = GenerateStreetAddress()
    city             = cities(Int(Rnd() * UBound(cities) + 1))
    state            = states(UBound(cities) - UBound(cities) + Int(Rnd() * (UBound(cities) + 1))) ' Keep city-state pair
    zipCode          = GenerateZipCode(city)
    
    ' Fix: Get proper city/state index pairing
    Dim cityIdx
    cityIdx = Int(Rnd() * (UBound(cities) + 1))
    city  = cities(cityIdx)
    state = states(cityIdx)
    zipCode = GenerateZipCodeByPrefix(zipPrefixes(cityIdx))

    outFile.WriteLine idNum & "," & firstName & " " & lastName & "," & sex & "," & birthDate & "," & _
                      phoneNumber & "," & altPhone & "," & email & "," & _
                      altContactName & "," & altContactNumber & "," & _
                      streetAddress & "," & city & "," & state & "," & zipCode
Next

outFile.Close
Set outFile = Nothing
Set fso = Nothing

WScript.Echo "Done! Generated " & NUM_RECORDS & " records in patients.csv"

' --- Helper Functions ---

Function PickFirstName()
    Dim isMale, idx
    isMale = (Int(Rnd() * 2) = 0)  ' 50/50 chance
    
    If isMale Then
        idx = Int(Rnd() * UBound(maleNames) + 1)
        PickFirstName = maleNames(idx)
    Else
        idx = Int(Rnd() * UBound(femaleNames) + 1)
        PickFirstName = femaleNames(idx)
    End If
End Function

Function InArray(val, arr)
    Dim j
    InArray = False
    For j = 0 To UBound(arr)
        If arr(j) = val Then
            InArray = True
            Exit Function
        End If
    Next
End Function

Function GeneratePhoneNumber()
    Dim area, prefix, suffix
    area  = 200 + Int(Rnd() * 799)  ' 200-999
    prefix = 100 + Int(Rnd() * 899) ' 100-998
    suffix = 1000 + Int(Rnd() * 8999) ' 1000-9998
    GeneratePhoneNumber = CStr(area) & "-" & FormatNumber(prefix, 3, -1, 0, 0) & "-" & FormatNumber(suffix, 4, -1, 0, 0)
End Function

Function FormatNumber(num, places, n, leftPad, zeroPad)
    Dim strVal
    strVal = CStr(num)
    Do While Len(strVal) < places
        strVal = "0" & strVal
    Loop
    FormatNumber = strVal
End Function

Function GenerateEmail(firstName, lastName)
    Dim domains, domain
    domains = Array("gmail.com","yahoo.com","hotmail.com","outlook.com","aol.com","icloud.com","mail.com")
    domain = domains(Int(Rnd() * UBound(domains) + 1))
    
    Dim numSuffix
    numSuffix = Int(Rnd() * 999)
    
    GenerateEmail = LCase(firstName) & "." & LCase(lastName) & CStr(numSuffix) & "@" & domain
End Function

Function GenerateAltContactName(patientSex)
    Dim relsFemale, relsMale
    relsFemale = Array("Spouse","Mother","Father","Daughter","Son","Sister","Brother","Friend","Neighbor","Caregiver")
    relsMale = Array("Spouse","Father","Mother","Son","Daughter","Brother","Sister","Friend","Neighbor","Caregiver")
    
    Dim relation, chosenName
    If patientSex = "M" Then
        relation = relsMale(Int(Rnd() * UBound(relsMale) + 1))
        If InArray(relation, Array("Father","Son","Brother")) Then
            chosenName = PickFirstNameFromList(maleNames)
        Else
            chosenName = PickFirstNameFromList(femaleNames)
        End If
    Else
        relation = relsFemale(Int(Rnd() * UBound(relsFemale) + 1))
        If InArray(relation, Array("Father","Son","Brother")) Then
            chosenName = PickFirstNameFromList(maleNames)
        Else
            chosenName = PickFirstNameFromList(femaleNames)
        End If
    End If
    
    GenerateAltContactName = chosenName & " " & lastNamesArray(Int(Rnd() * UBound(lastNamesArray) + 1))
End Function

Function PickFirstNameFromList(nameArray)
    PickFirstNameFromList = nameArray(Int(Rnd() * UBound(nameArray) + 1))
End Function

Function GenerateStreetAddress()
    Dim streetNum, streetName, stType
    streetNum = Int(Rnd() * 9998) + 1
    streetName = lastNamesArray(Int(Rnd() * UBound(lastNamesArray) + 1))
    stType = streetTypes(Int(Rnd() * UBound(streetTypes) + 1))
    GenerateStreetAddress = CStr(streetNum) & " " & streetName & " " & stType
End Function

Function GenerateZipCodeByPrefix(prefix)
    Dim suffix
    suffix = Int(Rnd() * 9999)
    GenerateZipCodeByPrefix = prefix & FormatNumber(suffix, 4, -1, 0, 0)
End Function

' Re-declare GenerateZipCode to use the fixed version
Function GenerateZipCode(dummyCity)
    Dim prefix
    prefix = "100" + Int(Rnd() * 9)
    GenerateZipCode = GenerateZipCodeByPrefix(CStr(prefix))
End Function