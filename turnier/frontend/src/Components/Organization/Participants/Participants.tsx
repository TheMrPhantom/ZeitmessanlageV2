import { Button, Checkbox, FormControl, InputLabel, MenuItem, Paper, Select, Stack, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, TextField, Tooltip, Typography } from '@mui/material'
import React, { useState } from 'react'
import DeleteIcon from '@mui/icons-material/Delete';
import style from './participants.module.scss'
import { Participant, SkillLevel, Size } from '../../../types/ResponseTypes';
import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useDispatch, useSelector } from 'react-redux';
import { useParams } from 'react-router-dom'
import { dateToURLString } from '../../Common/StaticFunctions';
import { addParticipant, removeParticipant, updateParticipant } from '../../../Actions/SampleAction';
import { isYouthParticipant, loadPermanent, storePermanent, stringToSize, stringToSkillLevel, updateDatabase } from '../../Common/StaticFunctionsTyped';
import { usePapaParse } from 'react-papaparse';
import ImportParticipants from './ImportParticipants';
import HowToRegIcon from '@mui/icons-material/HowToReg';
import AlarmOnIcon from '@mui/icons-material/AlarmOn';
import PaidIcon from '@mui/icons-material/Paid';
import { ArrowUpward, ArrowDownward } from '@mui/icons-material';
import HelpIcon from '@mui/icons-material/Help';

type Props = {}

type TableProps = {
    common: CommonReducerType,
    turnamentDate: Date,
    organization: string
}

const ParticipantTable = (props: TableProps) => {
    const dispatch = useDispatch();
    const [sortConfig, setSortConfig] = useState<{ key: string | null, direction: 'asc' | 'desc' | null }>({ key: null, direction: null });

    const participants = props.common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(props.turnamentDate))?.participants || [];

    // Sorting function
    const sortedParticipants = [...participants].sort((a: any, b: any) => {
        if (sortConfig.key) {
            const aValue = a[sortConfig.key];
            const bValue = b[sortConfig.key];

            if (aValue === bValue) return 0;

            const sortDirection = sortConfig.direction === 'asc' ? 1 : -1;
            return aValue > bValue ? sortDirection : -sortDirection;
        }
        // Default sorting by startNumber when no column is actively sorted
        return a.startNumber - b.startNumber;
    });

    // Handle sorting with three states: asc, desc, and default (unsorted)
    const handleSort = (key: string) => {
        // Prevent sorting on "Startnummer" and "Entfernen" columns
        if (key === 'startNumber' || key === 'delete') return;

        if (sortConfig.key === key) {
            if (sortConfig.direction === 'asc') {
                // Change from asc to desc
                setSortConfig({ key, direction: 'desc' });
            } else if (sortConfig.direction === 'desc') {
                // Change from desc to default (startNumber sorting)
                setSortConfig({ key: null, direction: null });
            } else {
                // Reset to asc
                setSortConfig({ key, direction: 'asc' });
            }
        } else {
            // Initial sort is ascending
            setSortConfig({ key, direction: 'asc' });
        }
    };

    // Helper to render sorting arrows
    const renderSortIcon = (key: string) => {
        if (sortConfig.key === key) {
            if (sortConfig.direction === 'asc') {
                return <ArrowUpward fontSize="small" />;
            } else if (sortConfig.direction === 'desc') {
                return <ArrowDownward fontSize="small" />;
            }
        }
        return null;
    };

    const updateParticipantFromTable = (participant: Participant) => {
        dispatch(updateParticipant(props.turnamentDate, participant));
        storePermanent(props.organization, props.common.organization);
        updateDatabase(props.common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(props.turnamentDate)), props.organization, dispatch);
    }
    return (
        <TableContainer component={Paper}>
            <Table size="small">
                <TableHead>
                    <TableRow>
                        <TableCell>Startnummer</TableCell>
                        <TableCell onClick={() => handleSort('name')} style={{ cursor: 'pointer' }}>
                            <Stack flexDirection="row" alignItems="center" gap={1}>
                                <div>Name</div>
                                {renderSortIcon('name')}
                            </Stack>
                        </TableCell>
                        <TableCell onClick={() => handleSort('club')} style={{ cursor: 'pointer' }}>
                            <Stack flexDirection="row" alignItems="center" gap={1}>
                                <div>Verein</div>
                                {renderSortIcon('club')}
                            </Stack>
                        </TableCell>
                        <TableCell onClick={() => handleSort('dog')} style={{ cursor: 'pointer' }}>
                            <Stack flexDirection="row" alignItems="center" gap={1}>
                                <div>Hund</div>
                                {renderSortIcon('dog')}
                            </Stack>
                        </TableCell>
                        <TableCell onClick={() => handleSort('skillLevel')} style={{ cursor: 'pointer' }}>
                            <Stack flexDirection="row" alignItems="center" gap={1}>
                                <div>Klasse</div>
                                {renderSortIcon('skillLevel')}
                            </Stack>
                        </TableCell>
                        <TableCell onClick={() => handleSort('size')} style={{ cursor: 'pointer' }}>
                            <Stack flexDirection="row" alignItems="center" gap={1}>
                                <div>Größe</div>
                                {renderSortIcon('size')}
                            </Stack>
                        </TableCell>
                        <Tooltip title="Meldung bezahlt?" arrow placement='top'>
                            <TableCell align="center" onClick={() => handleSort('paid')} style={{ cursor: 'pointer' }}>
                                <Stack flexDirection="row" alignItems="center" gap={1}>
                                    <PaidIcon />
                                    {renderSortIcon('paid')}
                                </Stack>
                            </TableCell>
                        </Tooltip>
                        <Tooltip title="Teilnehmer gemeldet?" arrow placement='top'>
                            <TableCell align="center" onClick={() => handleSort('registered')} style={{ cursor: 'pointer' }}>
                                <Stack flexDirection="row" alignItems="center" gap={1}>
                                    <HowToRegIcon />
                                    {renderSortIcon('registered')}
                                </Stack>
                            </TableCell>
                        </Tooltip>
                        <Tooltip title="Teilnehmer am Start?" arrow placement='top'>
                            <TableCell align="center" onClick={() => handleSort('ready')} style={{ cursor: 'pointer' }}>
                                <Stack flexDirection="row" alignItems="center" gap={1}>
                                    <AlarmOnIcon />
                                    {renderSortIcon('ready')}
                                </Stack>
                            </TableCell>
                        </Tooltip>

                        <TableCell>Entfernen</TableCell>
                    </TableRow>
                </TableHead>
                <TableBody>
                    {sortedParticipants.map((participant, index) => (
                        <TableRow key={index}>
                            <TableCell>{participant.startNumber}</TableCell>
                            <TableCell>{participant.name}</TableCell>
                            <TableCell>{participant.club}</TableCell>
                            <TableCell>{participant.dog}</TableCell>
                            <TableCell>
                                <FormControl className={style.picker}>
                                    <InputLabel id="demo-simple-select-label" >Klasse</InputLabel>
                                    <Select
                                        labelId="demo-simple-select-label"
                                        id="demo-simple-select"
                                        value={participant.skillLevel}
                                        label="Klasse"
                                        onChange={(value) => {
                                            participant.skillLevel = value.target.value as SkillLevel;
                                            updateParticipantFromTable(participant)
                                        }}
                                    >
                                        <MenuItem value={SkillLevel.A0}>A0</MenuItem>
                                        <MenuItem value={SkillLevel.A1}>A1</MenuItem>
                                        <MenuItem value={SkillLevel.A2}>A2</MenuItem>
                                        <MenuItem value={SkillLevel.A3}>A3</MenuItem>
                                    </Select>
                                </FormControl>
                            </TableCell>
                            <TableCell>
                                <FormControl className={style.picker}>
                                    <InputLabel id="demo-simple-select-label" >Größe</InputLabel>
                                    <Select
                                        labelId="demo-simple-select-label"
                                        id="demo-simple-select"
                                        value={participant.size}
                                        label="Größe"
                                        onChange={(value) => {
                                            participant.size = value.target.value as Size;
                                            updateParticipantFromTable(participant)
                                        }}
                                    >
                                        <MenuItem value={Size.Small}>Small</MenuItem>
                                        <MenuItem value={Size.Medium}>Medium</MenuItem>
                                        <MenuItem value={Size.Intermediate}>Intermediate</MenuItem>
                                        <MenuItem value={Size.Large}>Large</MenuItem>
                                    </Select>
                                </FormControl>
                            </TableCell>
                            <TableCell align="center">
                                <Checkbox
                                    checked={participant.paid}
                                    onChange={(value) => {
                                        participant.paid = value.target.checked;
                                        updateParticipantFromTable(participant)
                                    }}
                                />
                            </TableCell>
                            <TableCell align="center">
                                <Checkbox
                                    checked={participant.registered}
                                    onChange={(value) => {
                                        participant.registered = value.target.checked;
                                        updateParticipantFromTable(participant)
                                    }}
                                />
                            </TableCell>
                            <TableCell align="center">
                                <Checkbox
                                    checked={participant.ready}
                                    onChange={(value) => {
                                        participant.ready = value.target.checked;
                                        updateParticipantFromTable(participant)
                                    }}
                                />
                            </TableCell>
                            <TableCell>
                                <Button
                                    color="error"
                                    variant="outlined"
                                    onClick={() => {
                                        dispatch(removeParticipant(props.turnamentDate, participant));
                                        storePermanent(props.organization, props.common.organization);
                                        updateDatabase(props.common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(props.turnamentDate)), props.organization, dispatch);
                                    }}
                                >
                                    <DeleteIcon />
                                </Button>
                            </TableCell>
                        </TableRow>
                    ))}
                </TableBody>
            </Table>
        </TableContainer>
    );
};

const Participants = (props: Props) => {
    const { readString } = usePapaParse();

    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    //const navigate = useNavigate()
    const params = useParams()

    const tempdate = params.date ? new Date(params.date) : new Date()
    const tempturnamentDate = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(tempdate))?.date
    const turnamentDate = new Date(tempturnamentDate ? tempturnamentDate : tempdate)
    const organization = params.organization ? params.organization : ""

    const [name, setname] = useState("")
    const [club, setclub] = useState("")
    const [dog, setdog] = useState("")
    const [association, setassociation] = useState("")
    const [associationMemberNumber, setassociationMemberNumber] = useState("")
    const [chipNumber, setchipNumber] = useState("")
    const [runclass, setrunclass] = useState(SkillLevel.A3)
    const [size, setsize] = useState(Size.Small)
    const [file, setFile] = useState<null | File>(null);
    const [birthYear, setbirthYear] = useState(1900)

    const [parsedInputFile, setparsedInputFile] = useState<Array<{ date?: string, participants: Participant[] }> | null>(null)

    loadPermanent(organization, dispatch, common)

    const addParticipantToTurnament = (name: string, birthYear: string, club: string, dog: string, runclass: SkillLevel, size: Size, association: string, associationMemberNumber: string, chipNumber: string) => {

        const participant: Participant = {
            startNumber: 0,
            sorting: 0,
            name: name,
            isYouth: isYouthParticipant(`${birthYear}-01-01`, turnamentDate),
            club: club,
            dog: dog,
            skillLevel: runclass,
            size: size,
            association: association,
            associationMemberNumber: associationMemberNumber,
            chipNumber: chipNumber,
            resultA: {
                time: -2,
                faults: 0,
                refusals: 0,
                run: runclass * 2
            },
            resultJ: {
                time: -2,
                faults: 0,
                refusals: 0,
                run: runclass * 2 + 1
            }
        }

        //Get all participants from turnament
        const participants = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))?.participants
        //Get the highest startnumber
        const startNumber = participants && participants.length > 0 ? Math.max(...participants.map(p => p.startNumber)) : 0
        //Add 1 to the highest startnumber
        participant.startNumber = startNumber + 1

        //Get participants with same class and size
        const sameClass = participants?.filter(p => p.skillLevel === runclass && p.size === size)
        //Get the highest sorting
        const sorting = sameClass && sameClass.length > 0 ? Math.max(...sameClass.map(p => p.sorting)) : 0
        //Add 1 to the highest sorting
        participant.sorting = sorting + 1

        //Add the new participant to the participants
        dispatch(addParticipant(turnamentDate, participant))

        setname("")
        setclub("")
        setdog("")
        setassociation("")
        setassociationMemberNumber("")
        setchipNumber("")
        setrunclass(SkillLevel.A3)
        setsize(Size.Small)

        //Store the new participant in the local storage
        storePermanent(organization, common.organization)
        //Get turnament
        const t = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))
        updateDatabase(t, organization, dispatch)
    }

    const handleFileChange: React.ChangeEventHandler<HTMLInputElement> = (event) => {
        setFile(event.target.files !== null ? event.target.files[0] : null);
    };

    // Handle file upload and print CSV content
    const handleUploadOMA = () => {
        if (!file) {
            console.error('No file selected');
            return;
        }

        const reader = new FileReader();

        reader.onload = (event) => {
            if (event.target !== null) {
                const text = event.target.result;
                readString(text as string, {
                    worker: true,
                    encoding: "windows-1252",
                    complete: (results) => {
                        const data = results.data as Array<Array<string>>
                        /*Data structure of oma file:
                            Tournament line
                            Participant line
                            Participant line
                            ...
                            Tournament line
                            Participant line
                            Participant line
                            ...
                        */
                        console.log(data)
                        const parsed: Array<{ date: string, participants: Participant[] }> = []

                        let participants: Participant[] = []
                        let date = ""
                        data.forEach(line => {
                            if (line.length === 1) {
                                if (line[0].length > 0) {
                                    //Tournament line
                                    const dateOfTournament = line[0].split(",")[1]

                                    //Parse the date from dd.mm.yyyy to yyyy-mm-dd
                                    const dateArray = dateOfTournament.split(".")
                                    date = `${dateArray[2]}-${dateArray[1]}-${dateArray[0]}`


                                    parsed.push({ date: date, participants: [] })
                                    if (parsed.length > 1) {
                                        parsed[parsed.length - 2].participants = participants
                                    }
                                    participants = []
                                }
                            } else {
                                if (line.length > 0 && line[0] !== "UeID") {
                                    //Participant line
                                    if (line[29] !== "1") {
                                        return
                                    }

                                    const skillLevel = stringToSkillLevel(line[42])
                                    const size = stringToSize(line[41])

                                    const participant: Participant = {
                                        startNumber: 0,
                                        sorting: 0,
                                        name: `${line[2]} ${line[3]}`,
                                        isYouth: isYouthParticipant(String(line[4]), turnamentDate),
                                        club: line[6],
                                        dog: line[15],
                                        skillLevel: skillLevel,
                                        size: size,
                                        mail: line[5],
                                        association: line[7],
                                        associationMemberNumber: line[8],
                                        chipNumber: line[21],
                                        measureDog: line[44] === "1",
                                        registered: false,
                                        ready: false,
                                        paid: line[31] === "1",
                                        resultA: {
                                            time: -2,
                                            faults: 0,
                                            refusals: 0,
                                            run: skillLevel * 2
                                        },
                                        resultJ: {
                                            time: -2,
                                            faults: 0,
                                            refusals: 0,
                                            run: skillLevel * 2 + 1
                                        }
                                    }
                                    participants.push(participant)
                                }
                            }
                        })

                        if (participants.length > 0) {
                            parsed[parsed.length - 1].participants = participants
                        }
                        setparsedInputFile(parsed)
                    },
                });



                // Optionally parse and handle CSV data
                // const lines = text.split('\n');
                // lines.forEach(line => console.log(line));
                setFile(null);
            }
        };

        reader.readAsText(file, 'windows-1252');
    };

    const handleUploadWebmelden = () => {
        if (!file) {
            console.error('No file selected');
            return;
        }

        const reader = new FileReader();

        reader.onload = (event) => {
            if (event.target !== null) {
                const text = event.target.result;
                readString(text as string, {
                    worker: true,
                    encoding: "utf-8",
                    delimiter: "\t",
                    complete: (results) => {
                        const data = results.data as Array<Array<string>>
                        /*Webmelden has only the participants of one turnament*/

                        console.log(data)

                        let participants: Participant[] = []

                        let lineIndex = 0
                        data.forEach(line => {
                            lineIndex++;
                            if (lineIndex === 1 || (line[0] === "" && line[1] === "")) {
                                return;
                            }

                            const skillLevel = stringToSkillLevel(line[21])
                            const size = stringToSize(line[22])

                            const lastname = line[0]
                            //The last name is like MÜLLER make it Müller
                            const lastnameCapitalized = lastname.charAt(0).toUpperCase() + lastname.slice(1).toLowerCase()

                            const participant: Participant = {
                                startNumber: 0,
                                sorting: 0,
                                name: `${line[1]} ${lastnameCapitalized}`,
                                isYouth: isYouthParticipant(String(line[2]), turnamentDate),
                                club: line[10],
                                dog: line[11],
                                skillLevel: skillLevel,
                                size: size,
                                mail: line[32],
                                association: line[9],
                                associationMemberNumber: line[8],
                                chipNumber: line[18],
                                measureDog: line[33] === "1",
                                registered: false,
                                ready: false,
                                paid: line[30] === line[31],
                                resultA: {
                                    time: -2,
                                    faults: 0,
                                    refusals: 0,
                                    run: skillLevel * 2
                                },
                                resultJ: {
                                    time: -2,
                                    faults: 0,
                                    refusals: 0,
                                    run: skillLevel * 2 + 1
                                }
                            }
                            console.log(participant)
                            participants.push(participant)

                        })

                        setparsedInputFile([{ participants: participants }])

                    },
                });



                // Optionally parse and handle CSV data
                // const lines = text.split('\n');
                // lines.forEach(line => console.log(line));
                setFile(null);
            }
        };

        reader.readAsText(file, 'utf-8');
    };

    const lastYears = () => {
        // Get the list of the last 23 years
        const years = []
        const currentYear = new Date().getFullYear()
        for (let i = 0; i < 23; i++) {
            years.push(currentYear - i)
        }
        return years
    }


    return (<>
        <Stack gap={2} className={style.paper}>
            <Stack direction="row" flexWrap="wrap" gap={3}>

                <Stack flexDirection={"row"} flexWrap={'wrap'} gap={2}>
                    <Paper className={style.paper}>
                        <Stack gap={2}>
                            <Typography variant='h6'>Teilnehmer aus O.M.A. importieren</Typography>
                            <input
                                type="file"
                                name="file"
                                onChange={handleFileChange}
                            />
                            <Button variant="contained" color="primary" onClick={handleUploadOMA}>
                                Hochladen
                            </Button>
                        </Stack>
                    </Paper>
                    <Paper className={style.paper}>
                        <Stack gap={2}>
                            <Stack gap={1} flexDirection={"row"} alignItems="center">
                                <Typography variant='h6'>Teilnehmer aus Webmelden importieren</Typography>
                                <Tooltip title='Verwende den Export für "Simple Agiltiy" in Webmelden' arrow placement='top' >
                                    <HelpIcon color='warning' />
                                </Tooltip>
                            </Stack>
                            <input
                                type="file"
                                name="file"
                                onChange={handleFileChange}
                            />
                            <Button variant="contained" color="primary" onClick={handleUploadWebmelden}>
                                Hochladen
                            </Button>
                        </Stack>
                    </Paper>

                </Stack>

                <Paper className={style.paper}>
                    <Stack gap={2}>
                        <Typography variant='h6'>Teilnehmer manuell hinzufügen</Typography>
                        <Stack direction="row" flexWrap="wrap" gap={2}>
                            <TextField value={name} label="Name" onChange={(value) => setname(value.target.value)} />
                            <FormControl className={style.picker}>
                                <InputLabel id="demo-simple-select-label" >Geburtsjahr</InputLabel>
                                <Select
                                    labelId="demo-simple-select-label"
                                    id="demo-simple-select"
                                    value={birthYear}
                                    label="Geburtsjahr"
                                    onChange={(value) => setbirthYear(Number(value.target.value))}
                                >
                                    {lastYears().map(year => <MenuItem value={year}>{year}</MenuItem>)}
                                    <MenuItem value={1900}>{"< " + lastYears().slice(-1)[0]}</MenuItem>

                                </Select>
                            </FormControl>
                            <TextField value={club} label="Verein" onChange={(value) => setclub(value.target.value)} />
                            <TextField value={dog} label="Hundename" onChange={(value) => setdog(value.target.value)} />
                            <FormControl className={style.picker}>
                                <InputLabel id="demo-simple-select-label" >Klasse</InputLabel>
                                <Select
                                    labelId="demo-simple-select-label"
                                    id="demo-simple-select"
                                    value={runclass}
                                    label="Klasse"
                                    onChange={(value) => setrunclass(Number(value.target.value))}
                                >
                                    <MenuItem value={SkillLevel.A0}>A0</MenuItem>
                                    <MenuItem value={SkillLevel.A1}>A1</MenuItem>
                                    <MenuItem value={SkillLevel.A2}>A2</MenuItem>
                                    <MenuItem value={SkillLevel.A3}>A3</MenuItem>
                                </Select>
                            </FormControl>
                            <FormControl className={style.picker}>
                                <InputLabel id="demo-simple-select-label" >Größe</InputLabel>
                                <Select
                                    labelId="demo-simple-select-label"
                                    id="demo-simple-select"
                                    value={size}
                                    label="Größe"
                                    onChange={(value) => setsize(Number(value.target.value))}
                                >
                                    <MenuItem value={Size.Small}>Small</MenuItem>
                                    <MenuItem value={Size.Medium}>Medium</MenuItem>
                                    <MenuItem value={Size.Intermediate}>Intermediate</MenuItem>
                                    <MenuItem value={Size.Large}>Large</MenuItem>
                                </Select>
                            </FormControl>
                            <TextField value={association} label="Verband" onChange={(value) => setassociation(value.target.value)} />
                            <TextField value={associationMemberNumber} label="Mitgliedsnummer" onChange={(value) => setassociationMemberNumber(value.target.value)} />
                            <TextField value={chipNumber} label="Chipnummer" onChange={(value) => setchipNumber(value.target.value)} />

                        </Stack>
                        <Button variant="contained"
                            color="primary"
                            onClick={() => {
                                addParticipantToTurnament(name, birthYear.toString(), club, dog, runclass, size, association, associationMemberNumber, chipNumber)
                            }}
                        >Hinzufügen</Button>
                    </Stack>
                </Paper>
            </Stack>
            <Stack gap={2}>
                <Typography variant='h5'>Starter</Typography>
                <ParticipantTable common={common} turnamentDate={turnamentDate} organization={organization} />

            </Stack>
        </Stack>
        <ImportParticipants parsedInput={parsedInputFile} close={() => { setparsedInputFile(null) }} />
    </>
    )
}

export default Participants